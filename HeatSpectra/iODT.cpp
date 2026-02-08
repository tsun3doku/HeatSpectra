#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>

#include <iostream>
#include <fstream>
#include <cmath>
#include <complex>
#include <map>
#include <memory>
#include "glm/gtx/string_cast.hpp"
#include "glm/gtc/constants.hpp"
#include <chrono>
#include <algorithm>
#include <queue>
#include <thread>
#include <atomic>

#include "iODT.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"

std::ostream& operator<<(std::ostream& os, const GeodesicTracer::SurfacePoint::Type& type) {
    switch (type) {
    case GeodesicTracer::SurfacePoint::Type::VERTEX: os << "VERTEX"; break;
    case GeodesicTracer::SurfacePoint::Type::EDGE:   os << "EDGE";   break;
    case GeodesicTracer::SurfacePoint::Type::FACE:   os << "FACE";   break;
    default:                                         os << "UNKNOWN"; break;
    }
    return os;
}

iODT::iODT(Model& model, VulkanDevice& vulkanDevice, MemoryAllocator& allocator) 
    : model(model), vulkanDevice(vulkanDevice), allocator(allocator), tracer(intrinsicMesh), tracerInput(inputMesh) {
    
    // Build the input mesh 
    inputMesh.buildFromModel(model);
    auto& inputConn = inputMesh.getConnectivity();
       
    inputMesh.updateAllCornerAngles({});
    inputMesh.computeCornerScaledAngles();
    inputMesh.updateAllSignposts();

    inputMesh.computeVertexAngleScales();
    inputMesh.buildHalfedgeVectorsInVertex();
    inputMesh.buildHalfedgeVectorsInFace();

    // Build the intrinsic mesh 
    intrinsicMesh.buildFromModel(model);
    auto& conn = intrinsicMesh.getConnectivity();
    intrinsicMesh.updateAllCornerAngles({});
    intrinsicMesh.computeCornerScaledAngles();
    intrinsicMesh.updateAllSignposts();

    intrinsicMesh.computeVertexAngleScales();
    intrinsicMesh.buildHalfedgeVectorsInVertex();
    intrinsicMesh.buildHalfedgeVectorsInFace();

    // Initialize vertex locations for tracing   
    initializeVertexLocations();

    // All edges start as original
    const auto& edges = conn.getEdges();
    for (size_t i = 0; i < edges.size(); ++i) {
        if (edges[i].halfEdgeIdx != INVALID_INDEX) {
            conn.getEdges()[i].isOriginal = true;
        }
    }
    
    // Initialize supporting halfedge
    supportingHalfedge = std::make_unique<SupportingHalfedge>(inputMesh, intrinsicMesh, tracer, vulkanDevice, allocator);
    supportingHalfedge->initialize();
    supportingHalfedge->uploadToGPU();
}

iODT::~iODT() {
}

bool iODT::optimalDelaunayTriangulation(int maxIterations, double minAngleDegrees, double maxEdgeLength, double stepSize) {
    auto& conn = intrinsicMesh.getConnectivity();

    // Clear tracking 
    insertedVertices.clear();
    recentlySplit.clear();

    // 1) Delaunay phase
    std::cout << "\nDelaunay Flipping Phase " << std::endl;
    if (supportingHalfedge) {
        supportingHalfedge->makeDelaunay(maxIterations);
    } else {
        conn.makeDelaunay(maxIterations);
    }
    std::cout << " Done.\n";
    
    intrinsicMesh.computeCornerScaledAngles();
    intrinsicMesh.computeVertexAngleScales();
    intrinsicMesh.buildHalfedgeVectorsInFace();

    // 2) Refinement phase
    //std::cout << "\nDelaunay Refinement Phase " << std::endl;
    if (!delaunayRefinement(maxIterations, minAngleDegrees)) {
        std::cerr << " Delaunay refinement failed" << std::endl;
    }

    // 4) Repositioning phae
    //std::cout << "\nRepositioning Phase (max edge length " << std::endl;
    repositionInsertedVertices(maxIterations, 1e-4, maxEdgeLength, stepSize);
    std::cout << " Done.\n";

    // 5) (OPTIONAL) Push the intrinsic result back to the model and GPU for debugging
    //intrinsicMesh.applyToModel(model);
    //model.recreateBuffers();

    // 6) Upload updated supporting map to GPU
    if (supportingHalfedge) {
        std::cout << "\nUploading updated supporting map to GPU..." << std::endl;
        supportingHalfedge->uploadToGPU();
        supportingHalfedge->uploadIntrinsicTriangleData();
        supportingHalfedge->uploadIntrinsicVertexData();
    }

    return true;
}

void iODT::repositionInsertedVertices(int maxIters, double tol, double maxEdgeLength, double stepSize)
{
    auto& conn = intrinsicMesh.getConnectivity();
    auto& verts = conn.getVertices();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();


    const double EPS_LEN = 1e-12;        
    const double START_EPS = 1e-8;      

    for (int iter = 0; iter < maxIters; ++iter) {
        int splitCount = 0;

        // Collect edges to split
        std::cout << " Splitting long edges..." << std::endl;
        std::vector<std::pair<uint32_t, uint32_t>> edgesToSplit;
        for (uint32_t e = 0; e < edges.size(); ++e) {
            if (edges[e].halfEdgeIdx == INVALID_INDEX)
                continue;

            uint32_t he = edges[e].halfEdgeIdx;
            double length = edges[e].intrinsicLength;

            if (length > maxEdgeLength) {
                edgesToSplit.push_back({ e, he });
            }
        }

        // Split collected edges and track newly inserted vertices
        std::vector<uint32_t> newlyInsertedVertices;
        for (const auto& [edgeIdx, heIdx] : edgesToSplit) {
            uint32_t newVertex = INVALID_INDEX;
            uint32_t diagFront = INVALID_INDEX;
            uint32_t diagBack = INVALID_INDEX;

            if (splitEdge(edgeIdx, newVertex, diagFront, diagBack, heIdx, 0.5)) {
                insertedVertices.insert(newVertex);
                newlyInsertedVertices.push_back(newVertex);
                splitCount++;
            }
        }

        if (splitCount > 0) {            
            intrinsicMesh.buildHalfedgeVectorsInFace();

            // Perform local Delaunay operation on edges around newly inserted vertices
            for (uint32_t newV : newlyInsertedVertices) {
                if (newV >= conn.getVertices().size()) 
                    continue;

                std::vector<uint32_t> localEdges;
                for (uint32_t he : conn.getVertexHalfEdges(newV)) {
                    uint32_t edgeIdx = conn.getEdgeFromHalfEdge(he);
                    if (edgeIdx != HalfEdgeMesh::INVALID_INDEX && !conn.getEdges()[edgeIdx].isOriginal) {
                        localEdges.push_back(edgeIdx);
                    }
                }
                if (supportingHalfedge) {
                    supportingHalfedge->makeDelaunay(maxIters, &localEdges);
                } else {
                    conn.makeDelaunay(maxIters, &localEdges);
                }
            }
        }

        if (insertedVertices.empty()) {
            return;
        }
        // Vertex repositioning phase
        //std::cout << "[Reposition] Iteration " << (iter + 1) << " - Vertex repositioning phase\n";
        double maxMove = 0.0;
        int movedCount = 0;

        // Process each vertex
        int processedCount = 0;
        for (uint32_t vIdx : insertedVertices) {
            if (vIdx >= verts.size())
                continue;
            if (conn.isBoundaryVertex(vIdx))
                continue;

            // Calculate weighted circumcenter displacement
            uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
            int localRefIdx = -1;
            glm::dvec2 avgVec(0.0);
            double avgLen = 0.0;
            bool ok = computeWeightedCircumcenter(vIdx, refFace, localRefIdx, avgVec, avgLen);

            if (!ok || avgLen <= EPS_LEN)
                continue;

            // Damped average length
            double stepLen = avgLen * stepSize;

            // Record movement for convergence tracking
            double moveLen = stepLen;

            if (moveLen > tol)
                ++movedCount;
            if (moveLen > maxMove)
                maxMove = moveLen;

            // Trace from vertex along displacement vector on intrinsic mesh
            glm::dvec2 displacement2D = avgVec * stepSize;
            double stepLength = glm::length(displacement2D);

            if (stepLength < 1e-12) {
                continue;
            }

            glm::dvec2 dir2D = glm::normalize(displacement2D);

            // Build 2D ring with vertex at origin
            auto ring = conn.buildVertexRing2D(vIdx);

            if (ring.neighborVertexIndices.empty()) {
                continue;
            }

            // New position in ring coords is the displacement from origin
            glm::dvec2 newPos2D = avgVec * stepSize;

            // Check for triangle inversion
            bool wouldInvert = false;

            for (size_t i = 0; i < ring.neighborVertexIndices.size(); ++i) {
                size_t nextI = (i + 1) % ring.neighborVertexIndices.size();

                glm::dvec2 p_i = ring.neighborPositions2D[i];
                glm::dvec2 p_next = ring.neighborPositions2D[nextI];

                // Calculate signed area
                double det = (p_i.x - newPos2D.x) * (p_next.y - newPos2D.y) - (p_i.y - newPos2D.y) * (p_next.x - newPos2D.x);

                // If area is negative or zero, dont move
                if (det <= 1e-6) { 
                    wouldInvert = true;
                    break;
                }
            }

            if (wouldInvert) {
                continue; 
            }         

            auto outgoingHEs = conn.getVertexHalfEdges(vIdx);
            
            // Call updateRemoval for all incident halfedges before changing lengths
            if (supportingHalfedge) {
                for (uint32_t he : outgoingHEs) {
                    uint32_t mate = halfEdges[he].opposite;
                    if (mate != INVALID_INDEX) {
                        supportingHalfedge->updateRemoval(mate);
                    }
                }
            }
            
            // Update edge lengths
            for (size_t i = 0; i < ring.neighborVertexIndices.size(); ++i) {
                uint32_t neighborIdx = ring.neighborVertexIndices[i];
                uint32_t edgeIdx = ring.edgeIndices[i];
                glm::dvec2 neighborPos2D = ring.neighborPositions2D[i];

                double newLength = glm::length(neighborPos2D - newPos2D);
                edges[edgeIdx].intrinsicLength = newLength;
            }
            
            for (uint32_t fIdx : ring.faceIndices) {
                intrinsicMesh.updateCornerAnglesForFace(fIdx);
            }
            
            // Call updateInsertion for all incident halfedges after updating corner angles
            if (supportingHalfedge) {
                for (uint32_t he : outgoingHEs) {
                    uint32_t mate = halfEdges[he].opposite;
                    if (mate != INVALID_INDEX) {
                        supportingHalfedge->updateInsertion(mate);
                    }
                }
            }

            // Update the vertex's surface point correspondence on input mesh
            GeodesicTracer::SurfacePoint dummyLoc;
            dummyLoc.type = GeodesicTracer::SurfacePoint::Type::VERTEX;
            dummyLoc.elementId = vIdx;

            bool resolveSuccess = resolveVertex(vIdx, dummyLoc);
        }
        // Convergence test
        if (maxMove < tol) {
            break;
        }
    }

    intrinsicMesh.buildHalfedgeVectorsInFace();
    intrinsicMesh.buildHalfedgeVectorsInVertex();
    
    // Update signpost angles for all inserted vertices
    for (uint32_t vIdx : insertedVertices) {
        if (vIdx >= verts.size()) continue;
        auto outgoingHEs = conn.getVertexHalfEdges(vIdx);
        for (uint32_t heOut : outgoingHEs) {
            uint32_t heIn = halfEdges[heOut].opposite;
            if (heIn != HalfEdgeMesh::INVALID_INDEX) {
                intrinsicMesh.updateAngleFromCWNeighbor(heIn);
            }
        }
    }

    // Update 3D positions for inserted vertices based on their updated input locations
    for (uint32_t vIdx : insertedVertices) {
        auto it = intrinsicVertexLocations.find(vIdx);

        if (it == intrinsicVertexLocations.end())
            continue;

        const GeodesicTracer::SurfacePoint& sp = it->second;
        glm::dvec3 p3 = tracerInput.evaluateSurfacePoint(sp); 
        verts[vIdx].position = glm::vec3(p3);
    }
    //std::cout << "[Reposition] Repositioning complete\n";
}

bool iODT::delaunayRefinement(int maxIters, float minAngleDegrees) {    
    const float MAX_AREA = 100.0f;  // Outdated, better to rely on edge length control in repositionInsertedVertices
    const float MIN_AREA = 1e-5f;   // Stop refining small triangles
    const float MIN_ANGLE = minAngleDegrees * glm::pi<float>() / 180.0f;

    auto& conn = intrinsicMesh.getConnectivity();
    
    size_t candidateCount = 0;

    for (int iter = 0; iter < 100; ++iter) {
        std::cout << " Refinement iteration " << (iter + 1) << "\n";
   
        auto cands = findRefinementCandidates(MIN_ANGLE, MAX_AREA, MIN_AREA);
        if (cands.empty()) {
            std::cout << " Done.\n";
            return true;
        }
        
        // Check for divergence
        if (iter == 0) {
            candidateCount = cands.size();
        } else if (cands.size() > candidateCount) {
            return true;
        }

        std::sort(cands.begin(), cands.end(),
            [&](auto const& a, auto const& b) {
                return a.priority > b.priority;
            });

        int refinedThisIter = 0;
        const int MAX_PER_ITER = 500; 

        // Skip only for this iteration
        std::unordered_set<uint32_t> skipFaces;
        // snapshot of global insertedVertices to detect new inserts added this iteration
        std::unordered_set<uint32_t> prevInserted = insertedVertices;
        // Set of inserts in this iteration
        std::unordered_set<uint32_t> iterNewVerts;

        auto markNeighborsOf = [&](uint32_t v) {
            if (v >= conn.getVertices().size()) return;
            auto& H = conn.getHalfEdges();
            for (auto he : conn.getVertexHalfEdges(v)) {
                if (he == HalfEdgeMesh::INVALID_INDEX)
                    continue;
                uint32_t fidx = H[he].face;
                if (fidx != HalfEdgeMesh::INVALID_INDEX) {
                    // skip for the rest of this iteration
                    skipFaces.insert(fidx);
                }
            }
            };

        for (auto const& C : cands) {
            
            // Limit refinements per iteration
            if (refinedThisIter >= MAX_PER_ITER)
                break;

            // Mid iteration skipping
            if (skipFaces.count(C.faceIdx))
                continue;

            // Revalidate candidate
            float  areaNow = intrinsicMesh.computeFaceArea(C.faceIdx);
            double minAngNow = computeMinAngle(C.faceIdx);
            if (areaNow < MIN_AREA)
                continue;

            bool angleTooSmall = false;
            if (MIN_ANGLE > 0.f) {
                angleTooSmall = (minAngNow < MIN_ANGLE);
            }
            
            bool areaTooLarge = false;
            if (MAX_AREA > 0.f) {
                areaTooLarge = (areaNow > MAX_AREA);
            }
            
            if (!(angleTooSmall || areaTooLarge)) {
                // Not a candidate
                continue;
            }

            // Refine
            uint32_t newV = UINT32_MAX;
            bool success = insertCircumcenter(C.faceIdx, newV);
            if (!success)
                continue;

            refinedThisIter++;

            // Collect vertices for current iteration
            std::vector<uint32_t> createdNow;
            if (newV != UINT32_MAX && newV < conn.getVertices().size()) {
                if (!iterNewVerts.count(newV)) {
                    createdNow.push_back(newV);
                    iterNewVerts.insert(newV);
                }
            }
            else {
                // Detect new vertices by global set difference
                for (uint32_t v : insertedVertices) {
                    if (!prevInserted.count(v) && !iterNewVerts.count(v)) {
                        createdNow.push_back(v);
                        iterNewVerts.insert(v);
                    }
                }
            }

            // Local delaunay on all edges around inserted vertex
            if (newV != UINT32_MAX && newV < conn.getVertices().size()) {
                std::vector<uint32_t> localEdges;
                for (uint32_t he : conn.getVertexHalfEdges(newV)) {
                    uint32_t edgeIdx = conn.getEdgeFromHalfEdge(he);
                    if (edgeIdx != HalfEdgeMesh::INVALID_INDEX) {
                        localEdges.push_back(edgeIdx);
                    }
                }
                if (supportingHalfedge) {
                    supportingHalfedge->makeDelaunay(maxIters, &localEdges);
                } else {
                    conn.makeDelaunay(maxIters, &localEdges);
                }
            }

            // skip neighbors of newly created verts for the rest of this iteration
            for (uint32_t v : createdNow) {
                markNeighborsOf(v);
            }
        }

        if (refinedThisIter == 0) {
            //std::cout << "No refinement possible.\n";
            return true;
        }
        // Update halfedge vectors at the end of iteration
        intrinsicMesh.computeVertexAngleScales();
        intrinsicMesh.buildHalfedgeVectorsInVertex();
        intrinsicMesh.buildHalfedgeVectorsInFace();
    }

    std::cout << "Reached max iterations\n";
    return true;
}

std::vector<iODT::RefinementCandidate>iODT::findRefinementCandidates(float minAngleThreshold, float maxAreaThreshold, float minAreaThreshold) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto const& F = conn.getFaces();

    std::cout << "  [RefinementCandidates] minAngleThreshold (rad) = "
        << minAngleThreshold << " ("
        << (minAngleThreshold * 180.0f / glm::pi<float>())
        << " deg), maxAreaThreshold = " << maxAreaThreshold << "\n";
    
    // Find parallel candidates
    const size_t numThreads = std::min<size_t>(std::thread::hardware_concurrency(), 8);
    std::vector<std::vector<RefinementCandidate>> threadResults(numThreads);
    std::atomic<int> totalFaces(0), skippedMinArea(0), skippedZeroAngle(0), skippedNotBad(0), addedCandidates(0);

    auto processChunk = [&](size_t threadId, size_t start, size_t end) {
        threadResults[threadId].reserve((end - start) / 2);  
        
        for (size_t f = start; f < end; ++f) {
            totalFaces.fetch_add(1, std::memory_order_relaxed);

        if (F[f].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX)
            continue;

        float  area = intrinsicMesh.computeFaceArea(f);
        double minAng = computeMinAngle(f);

            if (area < minAreaThreshold) {
                skippedMinArea.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (minAng <= 0.0) {
                skippedZeroAngle.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            bool angleTooSmall = false;
            if (minAngleThreshold > 0.f) {
                angleTooSmall = (minAng < minAngleThreshold);
            }
            
            bool areaTooLarge = false;
            if (maxAreaThreshold > 0.f) {
                areaTooLarge = (area > maxAreaThreshold);
            }

            if (!(angleTooSmall || areaTooLarge)) {
                skippedNotBad.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            RefinementCandidate rc{};
            rc.faceIdx = static_cast<uint32_t>(f);
            rc.minAngle = minAng;
            rc.area = area;
            rc.type = RefinementType::CIRCUMCENTER_INSERTION;
            rc.edgeIdx = HalfEdgeMesh::INVALID_INDEX;

            // safe priority
            float scoreArea = (maxAreaThreshold > 0.f) ? (area / maxAreaThreshold) : 0.f;
            float scoreAngle = (minAngleThreshold > 0.f) ? ((minAngleThreshold - float(minAng)) / minAngleThreshold) : 0.f;
            rc.priority = scoreArea + scoreAngle;

            threadResults[threadId].push_back(rc);
            addedCandidates.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Launch threads
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    size_t facesPerThread = (F.size() + numThreads - 1) / numThreads;
    
    for (size_t t = 0; t < numThreads; ++t) {
        size_t start = t * facesPerThread;
        size_t end = std::min(start + facesPerThread, F.size());
        if (start < end) {
            threads.emplace_back(processChunk, t, start, end);
        }
    }
    
    // Wait for completion
    for (auto& thread : threads) {
        thread.join();
    }

    // Merge thread results
    std::vector<RefinementCandidate> out;
    size_t totalSize = 0;
    for (const auto& tr : threadResults) {
        totalSize += tr.size();
    }
    out.reserve(totalSize);
    for (auto& tr : threadResults) {
        out.insert(out.end(), tr.begin(), tr.end());
    }

    std::cout << "  [RefinementCandidates] Face filtering: total=" << totalFaces.load()
        << " skippedMinArea=" << skippedMinArea.load()
        << " skippedZeroAngle=" << skippedZeroAngle.load()
        << " skippedNotBad=" << skippedNotBad.load()
        << " candidates=" << addedCandidates.load() << std::endl;

    return out;
}

bool iODT::insertCircumcenter(uint32_t faceIdx, uint32_t& outNewVertex) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto& halfEdges = conn.getHalfEdges();
    auto& vertices = conn.getVertices();
    const auto& faces = conn.getFaces();

    //std::cout << "[insertCircumcenter] called on faceIdx = " << faceIdx << std::endl;

    // Validate face index
    if (faceIdx >= faces.size()) {
        return false;
    }
    if (faces[faceIdx].halfEdgeIdx == INVALID_INDEX) {
        return false;
    }

    // Reject tiny faces
    float area = intrinsicMesh.computeFaceArea(faceIdx);
    if (area < 1e-8f) {
        return false;
    }

    // Layout original triangle in 2d
    auto triangle2D = intrinsicMesh.layoutTriangle(faceIdx);
    glm::dvec2 P0 = triangle2D.vertices[0], P1 = triangle2D.vertices[1], P2 = triangle2D.vertices[2];

    // Calculate 2d circumcenter 
    glm::dvec2 cc2d = intrinsicMesh.computeCircumcenter2D(P0, P1, P2);
    if (!std::isfinite(cc2d.x) || !std::isfinite(cc2d.y))
        return false;

    //std::cout << "[insertCircumcenter] circumcenter 2D coords=(" << cc2d.x << "," << cc2d.y << ")" << std::endl;

    // Pick corner furthest the circumcenter (smallest bary)
    glm::dvec3 circumcenterBary = intrinsicMesh.computeBarycentric2D(cc2d, P0, P1, P2);
    
    // Ruppert check
    bool tooOblique = (circumcenterBary.x < -0.5 || circumcenterBary.x > 1.5 ||
                       circumcenterBary.y < -0.5 || circumcenterBary.y > 1.5 ||
                       circumcenterBary.z < -0.5 || circumcenterBary.z > 1.5);
    
    if (tooOblique) {
        // Split longest edge at midpoint
        uint32_t he0 = faces[faceIdx].halfEdgeIdx;
        uint32_t he1 = halfEdges[he0].next;
        uint32_t he2 = halfEdges[he1].next;
        
        double len0 = conn.getIntrinsicLengthFromHalfEdge(he0);
        double len1 = conn.getIntrinsicLengthFromHalfEdge(he1);
        double len2 = conn.getIntrinsicLengthFromHalfEdge(he2);
        
        uint32_t longestHe = he0;
        if (len1 > len0 && len1 >= len2) longestHe = he1;
        else if (len2 > len0 && len2 > len1) longestHe = he2;
        
        uint32_t edgeIdx = conn.getEdgeFromHalfEdge(longestHe);
        if (edgeIdx != HalfEdgeMesh::INVALID_INDEX) {
            uint32_t dummy1, dummy2;
            splitEdge(edgeIdx, outNewVertex, dummy1, dummy2, longestHe, 0.5);
            return true;
        }
    }
    
    int corner = 0;

    if (circumcenterBary.y < circumcenterBary.x)
        corner = 1;
    if (circumcenterBary.z < circumcenterBary[corner])
        corner = 2;

    constexpr double EPS = 1e-4;
    glm::dvec3 cornerB(EPS);
    cornerB[corner] = 1.0 - 2.0 * EPS;

    // Calculate the 2d start point inside the triangle
    glm::dvec2 start2D =
        P0 * cornerB.x +
        P1 * cornerB.y +
        P2 * cornerB.z;

    // Build intrinsic vector 
    glm::dvec2 dir2D = cc2d - start2D;
    double length = glm::length(dir2D);

    if (length < 1e-12)
        return false;

    dir2D /= length;

    // Trace across face/faces   
    auto faceHalfedges = conn.getFaceHalfEdges(faceIdx);
    uint32_t startVertex = HalfEdgeMesh::INVALID_INDEX;
    if (cornerB.z > 0.99)
        startVertex = faceHalfedges[2]; // mostly at corner 2
    else if (cornerB.y > 0.99)
        startVertex = faceHalfedges[1]; // mostly at corner 1  
    else if (cornerB.x > 0.99)
        startVertex = faceHalfedges[0]; // mostly at corner 0

    GeodesicTracer::GeodesicTraceResult intrinsicRes = tracer.traceFromFace(
        faceIdx,     // start face
        cornerB,     // start corner bary
        dir2D,       // 2D direction in chart
        length       // circumradius
    );

    if (!intrinsicRes.success) {
        return false;
    }

    // Pick insertion type
    GeodesicTracer::SurfacePoint& surfacePoint = intrinsicRes.exitPoint;

    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
        return false;
    }

    // Record point location
    GeodesicTracer::SurfacePoint pendingInsert = surfacePoint;

    if (surfacePoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // EDGE split
        uint32_t edgeIdx = surfacePoint.elementId;
        double split = surfacePoint.split;

        // Pick a halfedge on the edge to pass to splitEdge
        uint32_t heSplit = conn.getEdges()[edgeIdx].halfEdgeIdx;
        if (heSplit == HalfEdgeMesh::INVALID_INDEX) {
            return false;
        }

        auto& edges = conn.getEdges();
        uint32_t dummy1, dummy2; 
        splitEdge(edgeIdx, outNewVertex, dummy1, dummy2, heSplit, split);

        // Track the input mesh location
        if (supportingHalfedge && intrinsicVertexLocations.count(outNewVertex)) {
            supportingHalfedge->trackInsertedVertex(outNewVertex, intrinsicVertexLocations[outNewVertex]);
        }

        return true;
    }
    else {
        // FACE insert 
        uint32_t targetFace = surfacePoint.elementId;
        glm::dvec3 targetBary = surfacePoint.baryCoords;

        // Layout target face and calculate split radii
        auto triangleTarget = intrinsicMesh.layoutTriangle(targetFace);
        glm::dvec2 V0 = triangleTarget.vertices[0], V1 = triangleTarget.vertices[1], V2 = triangleTarget.vertices[2];
        glm::dvec2 s2D = V0 * targetBary.x + V1 * targetBary.y + V2 * targetBary.z;
        double R0 = glm::length(s2D - V0);
        double R1 = glm::length(s2D - V1);
        double R2 = glm::length(s2D - V2);

        //  Get the halfedge of the original face at the chosen corner
        auto startHEs = conn.getFaceHalfEdges(faceIdx);
        uint32_t intrinsicStartHe = startHEs[corner];
        uint32_t intrinsicStartVertex = halfEdges[intrinsicStartHe].origin;

        // Get the three original vertices of the face before splitting it
        auto oldHEs = conn.getFaceHalfEdges(targetFace);
        std::vector<uint32_t> oldVerts;
        for (uint32_t he : oldHEs) {
            oldVerts.push_back(conn.getHalfEdges()[he].origin);
        }

        // Call updateRemoval on boundary halfedges before topology changes
        if (supportingHalfedge) {
            for (uint32_t he : oldHEs) {
                supportingHalfedge->updateRemoval(he);
            }
        }

        // Create the vertex topologically
        uint32_t newV = conn.splitTriangleIntrinsic(targetFace, R0, R1, R2);
        if (newV == HalfEdgeMesh::INVALID_INDEX)
            return false;

        uint32_t baseIdx = static_cast<uint32_t>(halfEdges.size()) - 6;
        uint32_t he_to_v1 = baseIdx + 3;
        uint32_t he_to_v0 = baseIdx + 5;
        uint32_t he_to_v2 = baseIdx + 4;

        // Resize halfedge vectors buffer
        intrinsicMesh.getHalfedgeVectorsInVertex().resize(halfEdges.size(), glm::dvec2(0.0));

        // Get the three new faces surrounding the vertex
        auto newFaces = conn.getVertexFaces(newV);

        // Update the corner angles for only the new faces
        for (uint32_t fIdx : newFaces) {
            if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
                intrinsicMesh.updateCornerAnglesForFace(fIdx);
            }
        }

        // Update supporting halfedges after corner angles are set
        if (supportingHalfedge) {
            // Update the 3 internal halfedges from old vertices to new vertex
            if (he_to_v0 < halfEdges.size()) {
                uint32_t he_from_v0 = halfEdges[he_to_v0].opposite;
                if (he_from_v0 != HalfEdgeMesh::INVALID_INDEX) {
                    supportingHalfedge->updateInsertion(he_from_v0);
                }
            }
            if (he_to_v1 < halfEdges.size()) {
                uint32_t he_from_v1 = halfEdges[he_to_v1].opposite;
                if (he_from_v1 != HalfEdgeMesh::INVALID_INDEX) {
                    supportingHalfedge->updateInsertion(he_from_v1);
                }
            }
            if (he_to_v2 < halfEdges.size()) {
                uint32_t he_from_v2 = halfEdges[he_to_v2].opposite;
                if (he_from_v2 != HalfEdgeMesh::INVALID_INDEX) {
                    supportingHalfedge->updateInsertion(he_from_v2);
                }
            }
            
            // Update the original boundary halfedges that were reused
            for (uint32_t he : oldHEs) {
                supportingHalfedge->updateInsertion(he);
            }
        }

        // Set the new vertex target angle sum before calculating scales
        auto& vertexAngleSums = intrinsicMesh.getVertexAngleSums();
        vertexAngleSums.resize(std::max<size_t>(vertexAngleSums.size(), newV + 1));
        vertexAngleSums[newV] = 2.0 * glm::pi<double>();

        intrinsicMesh.computeVertexAngleScales();

        // Let resolveVertex handle signpost angle calculations
        if (!resolveVertex(newV, intrinsicRes.exitPoint)) {
            return false;
        }

        insertedVertices.insert(newV);
        outNewVertex = newV;

        // Track the input mesh location 
        if (supportingHalfedge && intrinsicVertexLocations.count(newV)) {
            supportingHalfedge->trackInsertedVertex(newV, intrinsicVertexLocations[newV]);
        }    

		return true;
	}
}

bool iODT::insertPoint(uint32_t faceIdx, const glm::dvec3& baryCoords, uint32_t& outVertex, bool* outWasInserted) {
	if (outWasInserted) {
		*outWasInserted = false;
	}

	auto& conn = intrinsicMesh.getConnectivity();
	auto& halfEdges = conn.getHalfEdges();
	const auto& faces = conn.getFaces();

	if (faceIdx >= faces.size() || faces[faceIdx].halfEdgeIdx == INVALID_INDEX) {
		return false;
	}

	const double EPS_BARY = 1e-8;

	glm::dvec3 b = baryCoords;
	if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) {
		return false;
	}

    b.x = std::max(0.0, b.x);
    b.y = std::max(0.0, b.y);
    b.z = std::max(0.0, b.z);
    double sum = b.x + b.y + b.z;
    if (sum <= 1e-16) {
        return false;
    }
    b /= sum;

    std::vector<uint32_t> faceHEs = conn.getFaceHalfEdges(faceIdx);
    if (faceHEs.size() != 3) {
        return false;
    }
    uint32_t he0 = faceHEs[0]; // v0 -> v1
    uint32_t he1 = faceHEs[1]; // v1 -> v2
    uint32_t he2 = faceHEs[2]; // v2 -> v0

    uint32_t v0 = halfEdges[he0].origin;
    uint32_t v1 = halfEdges[he1].origin;
    uint32_t v2 = halfEdges[he2].origin;

    // If the point is on an existing point then return it
    if (b.x >= 1.0 - EPS_BARY) {
        outVertex = v0;
        return true;
    }
    if (b.y >= 1.0 - EPS_BARY) {
        outVertex = v1;
        return true;
    }
    if (b.z >= 1.0 - EPS_BARY) {
        outVertex = v2;
        return true;
    }

    // If the point is on an edge then split it
    if (b.z <= EPS_BARY) {
        double denom = b.x + b.y;
        if (denom <= 1e-16) {
            return false;
        }
        double t = b.y / denom; // along v0->v1
        return splitEdge(he0, t, outVertex, outWasInserted);
    }

    if (b.x <= EPS_BARY) {
        double denom = b.y + b.z;
        if (denom <= 1e-16) {
            return false;
        }
        double t = b.z / denom; // along v1->v2
        return splitEdge(he1, t, outVertex, outWasInserted);
    }

    if (b.y <= EPS_BARY) {
        double denom = b.z + b.x;
        if (denom <= 1e-16) {
            return false;
        }
        double t = b.x / denom; // along v2->v0
        return splitEdge(he2, t, outVertex, outWasInserted);
    }

    // Else, insert into the face 
    auto tri2D = intrinsicMesh.layoutTriangle(faceIdx);
    glm::dvec2 P0 = tri2D.vertices[0];
    glm::dvec2 P1 = tri2D.vertices[1];
    glm::dvec2 P2 = tri2D.vertices[2];

    glm::dvec2 s2D = P0 * b.x + P1 * b.y + P2 * b.z;
    double R0 = glm::length(s2D - P0);
    double R1 = glm::length(s2D - P1);
    double R2 = glm::length(s2D - P2);

    // Call updateRemoval on boundary halfedges before topology changes
    if (supportingHalfedge) {
        for (uint32_t he : faceHEs) {
            supportingHalfedge->updateRemoval(he);
        }
    }

    uint32_t newV = conn.splitTriangleIntrinsic(faceIdx, R0, R1, R2);
    if (newV == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    // Internal halfedges from old vertices to newV are in the last 6 halfedges
    uint32_t baseIdx = static_cast<uint32_t>(halfEdges.size()) - 6;
    uint32_t he_to_v1 = baseIdx + 3; // v1 -> newV
    uint32_t he_to_v0 = baseIdx + 5; // v0 -> newV
    uint32_t he_to_v2 = baseIdx + 4; // v2 -> newV

    // Resize halfedge vectors buffer
    intrinsicMesh.getHalfedgeVectorsInVertex().resize(halfEdges.size(), glm::dvec2(0.0));

    // Update corner angles for only new faces around the inserted vertex
    auto newFaces = conn.getVertexFaces(newV);
    for (uint32_t fIdx : newFaces) {
        if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
            intrinsicMesh.updateCornerAnglesForFace(fIdx);
        }
    }

    // Update supporting halfedges after corner angles are set
    if (supportingHalfedge) {
        if (he_to_v0 < halfEdges.size()) {
            uint32_t he_from_v0 = halfEdges[he_to_v0].opposite;
            if (he_from_v0 != HalfEdgeMesh::INVALID_INDEX) {
                supportingHalfedge->updateInsertion(he_from_v0);
            }
        }
        if (he_to_v1 < halfEdges.size()) {
            uint32_t he_from_v1 = halfEdges[he_to_v1].opposite;
            if (he_from_v1 != HalfEdgeMesh::INVALID_INDEX) {
                supportingHalfedge->updateInsertion(he_from_v1);
            }
        }
        if (he_to_v2 < halfEdges.size()) {
            uint32_t he_from_v2 = halfEdges[he_to_v2].opposite;
            if (he_from_v2 != HalfEdgeMesh::INVALID_INDEX) {
                supportingHalfedge->updateInsertion(he_from_v2);
            }
        }

        // Update the original boundary halfedges that were reused
        for (uint32_t he : faceHEs) {
            supportingHalfedge->updateInsertion(he);
        }
    }

    // Set the new vertex target angle sum before calculating scales
    auto& vertexAngleSums = intrinsicMesh.getVertexAngleSums();
    vertexAngleSums.resize(std::max<size_t>(vertexAngleSums.size(), newV + 1));
    vertexAngleSums[newV] = 2.0 * glm::pi<double>();

    intrinsicMesh.computeVertexAngleScales();

    // Rebuild halfedge vectors after topology change for accurate tracing
    intrinsicMesh.buildHalfedgeVectorsInVertex();
    intrinsicMesh.buildHalfedgeVectorsInFace();

    GeodesicTracer::SurfacePoint sp;
    sp.type = GeodesicTracer::SurfacePoint::Type::FACE;
    sp.elementId = faceIdx;
    sp.baryCoords = b;
    sp.split = 0.0;
    if (!resolveVertex(newV, sp)) {
        return false;
    }

    insertedVertices.insert(newV);
    outVertex = newV;

    if (supportingHalfedge && intrinsicVertexLocations.count(newV)) {
        supportingHalfedge->trackInsertedVertex(newV, intrinsicVertexLocations[newV]);
    }
    if (outWasInserted) {
        *outWasInserted = true;
    }

    if (supportingHalfedge) {
        supportingHalfedge->makeDelaunay(2);
    }
    else {
        conn.makeDelaunay(2);
    }

    return true;
}

bool iODT::splitEdge(uint32_t heEdge, double tParam, uint32_t& outNewV, bool* outWasInserted) {
    if (outWasInserted) {
        *outWasInserted = false;
    }

    auto& conn = intrinsicMesh.getConnectivity();
    auto& halfEdges = conn.getHalfEdges();

    const double EPS_T = 1e-8;

    tParam = std::clamp(tParam, 0.0, 1.0);
    if (tParam <= EPS_T) {
        outNewV = halfEdges[heEdge].origin;
        return true;
    }
    uint32_t heNext = halfEdges[heEdge].next;
    if (heNext == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }
    if (1.0 - tParam <= EPS_T) {
        outNewV = halfEdges[heNext].origin;
        return true;
    }

    uint32_t edgeIdx = conn.getEdgeFromHalfEdge(heEdge);
    if (edgeIdx == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    uint32_t diagF = HalfEdgeMesh::INVALID_INDEX;
    uint32_t diagB = HalfEdgeMesh::INVALID_INDEX;
    uint32_t newV = HalfEdgeMesh::INVALID_INDEX;
    if (!splitEdge(edgeIdx, newV, diagF, diagB, heEdge, tParam)) {
        return false;
    }

    intrinsicMesh.buildHalfedgeVectorsInFace();

    if (supportingHalfedge && intrinsicVertexLocations.count(newV)) {
        supportingHalfedge->trackInsertedVertex(newV, intrinsicVertexLocations[newV]);
    }
    if (outWasInserted) {
        *outWasInserted = true;
    }

    if (supportingHalfedge) {
        supportingHalfedge->makeDelaunay(2);
    }
    else {
        conn.makeDelaunay(2);
    }

    outNewV = newV;
    return true;
}

bool iODT::splitEdge(uint32_t edgeIdx, uint32_t& outNewVertex, uint32_t& outDiagFront, uint32_t& outDiagBack, uint32_t HESplit, double t) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto& verts = conn.getVertices();
    auto& edges = conn.getEdges();
    auto& halfEdges = conn.getHalfEdges();
    auto& faces = conn.getFaces();

    if (edgeIdx >= edges.size()) {
        //std::cout << "[splitEdge] Invalid edge index\n";
        return false;
    }

    // Use the passed in halfedge 
    uint32_t parentHE = HESplit;
    uint32_t oppHE = halfEdges[parentHE].opposite;

    // Verify the halfedge actually belongs to this edge
    uint32_t edgeFromHE = conn.getEdgeFromHalfEdge(HESplit);
    if (edgeFromHE != edgeIdx) {
        return false;
    }

    uint32_t originalVA = halfEdges[parentHE].origin;
    uint32_t originalVB = halfEdges[halfEdges[parentHE].next].origin;

    // Set split fraction
    double splitFraction = t;

    // Get original edge length and calculate child lengths
    double originalLength = conn.getIntrinsicLengthFromHalfEdge(parentHE);

    if (originalLength <= 1e-12) {
        return false;
    }

    double La = splitFraction * originalLength;
    double Lb = (1.0 - splitFraction) * originalLength;

    // Precalculate diagonal lengths before split
    std::vector<double> precomputedDiagonals;
    uint32_t face1 = halfEdges[parentHE].face;
    uint32_t face2 = (oppHE != HalfEdgeMesh::INVALID_INDEX)
        ? halfEdges[oppHE].face : HalfEdgeMesh::INVALID_INDEX;

    double diagLen1 = 0.0;
    if (face1 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen1 = intrinsicMesh.computeSplitDiagonalLength(face1, originalVA, originalVB, splitFraction);
    }

    double diagLen2 = 0.0;
    if (face2 != HalfEdgeMesh::INVALID_INDEX) {
        diagLen2 = intrinsicMesh.computeSplitDiagonalLength(face2, originalVA, originalVB, splitFraction);
    }

    precomputedDiagonals = { diagLen1, diagLen2 };

    // Call updateRemoval on the original edge halfedges before topology changes
    if (supportingHalfedge) {
        supportingHalfedge->updateRemoval(parentHE);
        if (oppHE != HalfEdgeMesh::INVALID_INDEX) {
            supportingHalfedge->updateRemoval(oppHE);
        }
    }

    // 5) Topology split
    edges[edgeIdx].halfEdgeIdx = parentHE;
    auto R = conn.splitEdgeTopo(edgeIdx, splitFraction);
    if (R.newV == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    uint32_t newV = R.newV;
    uint32_t diagFront = R.diagFront;
    uint32_t diagBack = R.diagBack;
    conn.getVertices()[newV].halfEdgeIdx = R.heB;

    // Set diagonal lengths before corner angle calculations
    for (int i = 0; i < 2; ++i) {
        uint32_t diagHE = (i == 0 ? R.diagFront : R.diagBack);
        double L = precomputedDiagonals[i];
        if (diagHE == HalfEdgeMesh::INVALID_INDEX)
            continue;

        uint32_t diagEdgeIdx = conn.getEdgeFromHalfEdge(diagHE);
        if (diagEdgeIdx != HalfEdgeMesh::INVALID_INDEX && diagEdgeIdx < edges.size()) {
            edges[diagEdgeIdx].intrinsicLength = L;
        }
    }

    // Resize buffers for the new vertex
    intrinsicMesh.getVertexAngleScales().resize(conn.getVertices().size(), 1.0);
    intrinsicMesh.getVertexAngleSums().resize(conn.getVertices().size(), 2.0 * glm::pi<double>());
    intrinsicMesh.getHalfedgeVectorsInVertex().resize(conn.getHalfEdges().size(), glm::dvec2(0.0));

    // Set the target angle sum for the new vertex
    bool isBoundary = (oppHE == HalfEdgeMesh::INVALID_INDEX ||
        halfEdges[oppHE].face == HalfEdgeMesh::INVALID_INDEX);
    intrinsicMesh.getVertexAngleSums()[newV] = isBoundary ? glm::pi<double>() : 2.0 * glm::pi<double>();

    // Update the corner angles only for affected faces
    auto adjacentFaces = conn.getVertexFaces(newV);
    for (uint32_t fIdx : adjacentFaces) {
        if (fIdx != HalfEdgeMesh::INVALID_INDEX) {
            intrinsicMesh.updateCornerAnglesForFace(fIdx);
        }
    }

    // Update supporting halfedges
    if (supportingHalfedge) {
        // Update all halfedges incident to the new vertex
        auto newVertexHEs = conn.getVertexHalfEdges(newV);
        for (uint32_t he : newVertexHEs) {
            supportingHalfedge->updateInsertion(he);
            uint32_t opp = halfEdges[he].opposite;
            if (opp != HalfEdgeMesh::INVALID_INDEX) {
                supportingHalfedge->updateInsertion(opp);
            }
        }
    }

    // Create surface point for the split location on the intrinsic mesh
    GeodesicTracer::SurfacePoint loc;
    loc.type = GeodesicTracer::SurfacePoint::Type::EDGE;
    loc.elementId = edgeIdx; // The intrinsic edge being split
    loc.split = splitFraction;
    loc.baryCoords = glm::dvec3(1.0 - splitFraction, splitFraction, 0.0);

    if (!resolveVertex(newV, loc)) {
        return false;
    }

    // Track inserted vertex and inserted surface point location
    insertedVertices.insert(newV);

    outNewVertex = newV;
    outDiagFront = diagFront;
    outDiagBack = diagBack;

    return true;
}

bool iODT::computeWeightedCircumcenter(uint32_t vertIdx, uint32_t& outRefFace, int& outLocalRefIdx, glm::dvec2& outAvgVec, double& outAvgLen) {
    const double EPS_LEN = 1e-12;

    auto& conn = intrinsicMesh.getConnectivity();
    const auto& HEs = conn.getHalfEdges();

    outRefFace = HalfEdgeMesh::INVALID_INDEX;
    outLocalRefIdx = -1;
    outAvgVec = glm::dvec2(0.0);
    outAvgLen = 0.0;

    // Build 2D ring with vertex at origin 
    auto ring = conn.buildVertexRing2D(vertIdx);
    if (ring.neighborVertexIndices.empty()) {
        return false;
    }

    // Pick first face as reference 
    if (!ring.faceIndices.empty()) {
        outRefFace = ring.faceIndices[0];
        outLocalRefIdx = 0; // Vertex is at origin in ring coords
    }

    // Calculate area weighted average of vectors to circumcenters in ring coordinates
    glm::dvec2 accum(0.0);
    double accumW = 0.0;
    int successCount = 0;

    // Calculate circumcenter for each neighbor pair
    for (size_t i = 0; i < ring.neighborVertexIndices.size(); ++i) {
        size_t nextI = (i + 1) % ring.neighborVertexIndices.size();

        // Find which face this corresponds to (face with halfedge from center to neighbor[i])
        uint32_t f = HalfEdgeMesh::INVALID_INDEX;
        if (i < ring.faceIndices.size()) {
            f = ring.faceIndices[i];
        }

        if (f == HalfEdgeMesh::INVALID_INDEX)
            continue;

        double area = intrinsicMesh.computeFaceArea(f);
        if (!(area > 0.0))
            continue;

        // Get the face's vertices in ring coordinates
        glm::dvec2 v0 = glm::dvec2(0.0, 0.0); 
        glm::dvec2 v1 = ring.neighborPositions2D[i];
        glm::dvec2 v2 = ring.neighborPositions2D[nextI];

        // Calculate circumcenter in ring coordinates
        glm::dvec2 cc2d = intrinsicMesh.computeCircumcenter2D(v0, v1, v2);
        if (!std::isfinite(cc2d.x) || !std::isfinite(cc2d.y)) {
            continue;
        }

        // Vector from vertex (at origin) to circumcenter
        glm::dvec2 vectorToCircumcenter = cc2d;

        double L = glm::length(vectorToCircumcenter);
        if (L <= EPS_LEN)
            continue;

        // Area weighted accumulation
        accum += area * vectorToCircumcenter;
        accumW += area;
        ++successCount;
    }

    if (!(accumW > 0.0) || successCount == 0) {
        return false;
    }

    outAvgVec = accum / accumW;
    outAvgLen = glm::length(outAvgVec);

    return true;
}

bool iODT::resolveVertex(uint32_t newVertexIdx, const GeodesicTracer::SurfacePoint& intrinsicPoint) {
    auto& conn = intrinsicMesh.getConnectivity();
    auto& inputConn = inputMesh.getConnectivity();
    const auto& verts = conn.getVertices();
    const auto& halfEdges = conn.getHalfEdges();

    // Calculate angular coordinates for the halfedges
    auto outgoingHEs = conn.getVertexHalfEdges(newVertexIdx);
    std::vector<uint32_t> incomingHEs;
    for (uint32_t heOut : outgoingHEs) {
        uint32_t twinHe = halfEdges[heOut].opposite;
        if (twinHe != HalfEdgeMesh::INVALID_INDEX) {
            incomingHEs.push_back(twinHe);
        }
    }

    // Call updateAngleFromCWNeighbor on all incoming halfedges first 
    for (uint32_t heIn : incomingHEs) {
        double originalAngle = halfEdges[heIn].signpostAngle;
        intrinsicMesh.updateAngleFromCWNeighbor(heIn);
    }

    if (incomingHEs.empty()) {
        return false;
    }

    // Choose best adjacent vertex to trace from
    // Priority: original vertices (1) > inserted vertices (2) > boundary vertices (3)
    uint32_t inputTraceHe = incomingHEs[0]; // default
    int bestPriority = 9999;
    double bestLength = std::numeric_limits<double>::max();

    for (uint32_t heIn : incomingHEs) {
        uint32_t adjacentVertex = halfEdges[heIn].origin;

        // Skip vertices that dont have input location mapping
        if (intrinsicVertexLocations.find(adjacentVertex) == intrinsicVertexLocations.end()) {
            continue;
        }

        int priority = 2; // Default: inserted vertex

        // Check if its an original vertex 
        if (verts[adjacentVertex].originalIndex != HalfEdgeMesh::INVALID_INDEX) {
            priority = 1; // Original vertices have most stable locations
        }

        // Check if its a boundary edge
        uint32_t edgeIdx = halfEdges[heIn].edgeIdx;
        if (edgeIdx != HalfEdgeMesh::INVALID_INDEX) {
            const auto& edges = conn.getEdges();
            if (edgeIdx < edges.size()) {
                uint32_t oppositeHe = halfEdges[heIn].opposite;
                if (oppositeHe == HalfEdgeMesh::INVALID_INDEX ||
                    halfEdges[oppositeHe].face == HalfEdgeMesh::INVALID_INDEX) {
                    priority = 3;
                }
            }
        }

        // Calculate edge length for tie break
        double edgeLength = conn.getIntrinsicLengthFromHalfEdge(heIn);

        // Select best candidate 
        if (priority < bestPriority || (priority == bestPriority && edgeLength < bestLength)) {
            bestPriority = priority;
            bestLength = edgeLength;
            inputTraceHe = heIn;
        }
    }

    // Get the adjacent vertex to trace from 
    uint32_t traceFromVertex = halfEdges[inputTraceHe].origin;

    // Make sure trace vertex has input location mapping
    if (intrinsicVertexLocations.find(traceFromVertex) == intrinsicVertexLocations.end()) {
        // Initialize original vertex location
        if (traceFromVertex < verts.size() && verts[traceFromVertex].originalIndex != HalfEdgeMesh::INVALID_INDEX) {
            GeodesicTracer::SurfacePoint location;
            location.type = GeodesicTracer::SurfacePoint::Type::VERTEX;
            location.elementId = verts[traceFromVertex].originalIndex;
            location.baryCoords = glm::dvec3(1.0, 0.0, 0.0);
            location.split = 0.0;
            intrinsicVertexLocations[traceFromVertex] = location;
        }
        else {
            return false;
        }
    }

    // Trace from adjacent vertex to new vertex on input mesh 
    GeodesicTracer::SurfacePoint startPoint = intrinsicVertexLocations[traceFromVertex];

    // Get the outgoing halfedge from traceFromVertex that points to newVertexIdx
    uint32_t outgoingTraceHe = halfEdges[inputTraceHe].opposite;
    if (outgoingTraceHe == HalfEdgeMesh::INVALID_INDEX) {
        return false;
    }

    // Get intrinsic direction vector from the adjacent vertex's frame
    glm::dvec2 intrinsicTraceVec = intrinsicMesh.halfedgeVector(inputTraceHe);
    double traceLength = conn.getIntrinsicLengthFromHalfEdge(inputTraceHe);

    // Trace on input mesh to find where new vertex should be placed
    GeodesicTracer::GeodesicTraceResult inputTrace;
    if (startPoint.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
        // Find reference face for vertex tracing
        auto outgoingHEs_input = inputConn.getVertexHalfEdges(startPoint.elementId);
        uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
        for (uint32_t he : outgoingHEs_input) {
            uint32_t face = inputConn.getHalfEdges()[he].face;
            if (face != HalfEdgeMesh::INVALID_INDEX) {
                refFace = face;
                break;
            }
        }

        if (refFace != HalfEdgeMesh::INVALID_INDEX) {
            GeodesicTracer::GeodesicTraceResult baseResult;
            baseResult.success = true;
            baseResult.distance = 0.0;
            inputTrace = tracerInput.traceFromVertex(startPoint.elementId, refFace, intrinsicTraceVec, traceLength, baseResult, traceLength);
        }
    }
    else if (startPoint.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // Get resolution face for this vertex
        uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
        auto rfIt = vertexResolutionFaces.find(traceFromVertex);
        if (rfIt != vertexResolutionFaces.end()) {
            refFace = rfIt->second;
        }
        inputTrace = tracerInput.traceFromEdge(startPoint.elementId, startPoint.split, intrinsicTraceVec, traceLength, outgoingTraceHe, refFace);
    }
    else if (startPoint.type == GeodesicTracer::SurfacePoint::Type::FACE) {
        inputTrace = tracerInput.traceFromFace(startPoint.elementId, startPoint.baryCoords, intrinsicTraceVec, traceLength);
    }

    if (!inputTrace.success) {
        return false;
    }

    // Set vertex location on input mesh using the actual exit point from trace
    GeodesicTracer::SurfacePoint newInputLocation = inputTrace.exitPoint;
    intrinsicVertexLocations[newVertexIdx] = newInputLocation;

    // Store which input face was used for tracing for tangent space alignment
    uint32_t resolutionFace = inputTrace.finalFaceIdx;
    if (resolutionFace == HalfEdgeMesh::INVALID_INDEX && !inputTrace.steps.empty()) {
        // Fallback: use the last face 
        resolutionFace = inputTrace.steps.back().finalBary.x >= 0 ? inputTrace.finalFaceIdx : HalfEdgeMesh::INVALID_INDEX;
    }
    if (resolutionFace != HalfEdgeMesh::INVALID_INDEX) {
        vertexResolutionFaces[newVertexIdx] = resolutionFace;
        //std::cout << "[resolveVertex] Stored resolution face=" << resolutionFace << " for vertex " << newVertexIdx << std::endl;
    }

    // Set the actual 3D position from the trace result
    auto& verticesMutable = const_cast<std::vector<HalfEdgeMesh::Vertex>&>(verts);
    verticesMutable[newVertexIdx].position = inputTrace.position3D;

    // Get the arrival direction from the input mesh trace
    glm::dvec2 outgoingVec(0.0);
    if (!inputTrace.steps.empty()) {
        const auto& finalStep = inputTrace.steps.back();
        // This is the outgoing vector 
        outgoingVec = -finalStep.dir2D;
    }
    else {
        outgoingVec = glm::dvec2(1.0, 0.0);
    }

    // Calculate incoming angle
    double incomingAngle = std::atan2(outgoingVec.y, outgoingVec.x);
    double standardizedAngle = intrinsicMesh.standardizeAngleForVertex(newVertexIdx, incomingAngle);

    // Set incoming = 0 for boundary halfedges
    if (!conn.isInteriorHalfEdge(inputTraceHe)) {
        standardizedAngle = 0.0;
    }

    // Set signpost angle for the traced halfedge opposite
    auto& halfEdgesMutable = const_cast<std::vector<HalfEdgeMesh::HalfEdge>&>(halfEdges);
    if (outgoingTraceHe != HalfEdgeMesh::INVALID_INDEX) {
        halfEdgesMutable[outgoingTraceHe].signpostAngle = standardizedAngle;
    }

    uint32_t firstHe = outgoingTraceHe;
    uint32_t currHe = HalfEdgeMesh::INVALID_INDEX;

    // Find currHe = firstHe.next().next().opp()
    if (firstHe != HalfEdgeMesh::INVALID_INDEX) {
        uint32_t nextHe = halfEdges[firstHe].next;
        if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
            nextHe = halfEdges[nextHe].next;
            if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
                currHe = halfEdges[nextHe].opposite;
            }
        }
    }

    // Orbit CCW updating angles
    while (currHe != HalfEdgeMesh::INVALID_INDEX && currHe != firstHe) {
        intrinsicMesh.updateAngleFromCWNeighbor(currHe);

        // Check if this is a boundary halfedge
        if (!conn.isInteriorHalfEdge(currHe)) {
            break;
        }

        // Move to next: currHe = currHe.next().next().opp()
        uint32_t nextHe = halfEdges[currHe].next;
        if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
            nextHe = halfEdges[nextHe].next;
            if (nextHe != HalfEdgeMesh::INVALID_INDEX) {
                currHe = halfEdges[nextHe].opposite;
            }
            else {
                break;
            }
        }
        else {
            break;
        }
    }
    return true;
}

double iODT::computeMinAngle(uint32_t faceIdx) {
    const auto& conn = intrinsicMesh.getConnectivity();
    const auto& faces = conn.getFaces();
    const auto& halfEdges = conn.getHalfEdges();

    if (faceIdx >= faces.size()) {
        return 0.0f;
    }

    // Get the halfedges of this face
    std::vector<uint32_t> faceEdges = conn.getFaceHalfEdges(faceIdx);
    if (faceEdges.size() != 3) {
        return 0.0f;
    }

    // Get the edge lengths from edge objects
    double a = conn.getIntrinsicLengthFromHalfEdge(faceEdges[0]);
    double b = conn.getIntrinsicLengthFromHalfEdge(faceEdges[1]);
    double c = conn.getIntrinsicLengthFromHalfEdge(faceEdges[2]);

    // Minimum positive length for numerical stability
    a = std::max(a, 1e-5);
    b = std::max(b, 1e-5);
    c = std::max(c, 1e-5);

    // Calculate angles using the law of cosines
    double cosA = (b * b + c * c - a * a) / (2.0f * b * c);
    double cosB = (a * a + c * c - b * b) / (2.0f * a * c);
    double cosC = (a * a + b * b - c * c) / (2.0f * a * b);

    // Clamp to valid range to avoid numerical issues
    cosA = glm::clamp(cosA, -1.0, 1.0);
    cosB = glm::clamp(cosB, -1.0, 1.0);
    cosC = glm::clamp(cosC, -1.0, 1.0);

    // Convert to angles
    double angleA = std::acos(cosA);
    double angleB = std::acos(cosB);
    double angleC = std::acos(cosC);

    // Return the minimum angle
    return std::min(std::min(angleA, angleB), angleC);
}

bool iODT::isEdgeOriginal(uint32_t edgeIdx) const {
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& edges = conn.getEdges();
    return edgeIdx < edges.size() ? edges[edgeIdx].isOriginal : false;
}

void iODT::initializeVertexLocations() {
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& vertices = conn.getVertices();

    // Initialize all original vertices to map 1:1 to input model vertices
    for (uint32_t vIdx = 0; vIdx < vertices.size(); ++vIdx) {
        if (vertices[vIdx].halfEdgeIdx == HalfEdgeMesh::INVALID_INDEX) 
            continue;

        // Initialize all original vertices 
        if (vertices[vIdx].originalIndex != HalfEdgeMesh::INVALID_INDEX) {
            GeodesicTracer::SurfacePoint location;
            location.type = GeodesicTracer::SurfacePoint::Type::VERTEX;
            location.elementId = vertices[vIdx].originalIndex;
            location.baryCoords = glm::dvec3(1.0, 0.0, 0.0);
            location.split = 0.0;

            intrinsicVertexLocations[vIdx] = location;
        }
    }
}

void iODT::updateVertexLocation(uint32_t intrinsicVertexId, const GeodesicTracer::SurfacePoint& locationOnInput) {
    intrinsicVertexLocations[intrinsicVertexId] = locationOnInput;
}

GeodesicTracer::GeodesicTraceResult iODT::traceIntrinsicHalfedgeAlongInput(uint32_t intrinsicHalfedgeIdx) {
    auto& conn = intrinsicMesh.getConnectivity();
    const auto& HEs = conn.getHalfEdges();
    const auto& inputVertices = inputMesh.getConnectivity().getVertices();

    if (intrinsicHalfedgeIdx >= HEs.size())
        return {};

    const auto& intrinsicHE = HEs[intrinsicHalfedgeIdx];
    uint32_t intrinsicStartV = intrinsicHE.origin;
    uint32_t intrinsicEndV = HEs[intrinsicHE.next].origin;
    uint32_t intrinsicEdgeIdx = conn.getEdgeFromHalfEdge(intrinsicHalfedgeIdx);

    // Short circuit for original edges
    if (isEdgeOriginal(intrinsicEdgeIdx)) {
        auto itA = intrinsicVertexLocations.find(intrinsicStartV);
        auto itB = intrinsicVertexLocations.find(intrinsicEndV);
        if (itA == intrinsicVertexLocations.end() || itB == intrinsicVertexLocations.end()) {
            //std::cout << "[traceIntrinsicHalfedge] Missing vertex locations for original edge\n";
            return {};
        }
        GeodesicTracer::GeodesicTraceResult result;
        result.success = true;
        result.pathPoints = { itA->second, itB->second };
        result.distance = glm::length(intrinsicMesh.halfedgeVector(intrinsicHalfedgeIdx));
        
        // Add a single step
        GeodesicTracer::FaceStepResult step;
        step.success = true;
        step.distanceTraveled = result.distance;
        result.steps.push_back(step);
        
        return result;
    }

    // Lookup mapped endpoints
    auto itStart = intrinsicVertexLocations.find(intrinsicStartV);
    auto itEnd = intrinsicVertexLocations.find(intrinsicEndV);

    if (itStart == intrinsicVertexLocations.end()) {
        //std::cout << "[traceIntrinsicHalfedge] Missing input mapping for start vertex " << intrinsicStartV << std::endl;
        return {};
    }
    if (itEnd == intrinsicVertexLocations.end()) {
        //std::cout << "[traceIntrinsicHalfedge] Missing input mapping for end vertex " << intrinsicEndV << std::endl;
        return {};
    }

    // Check if the mapped vertices exist in input mesh
    GeodesicTracer::SurfacePoint startSP = itStart->second;
    if (startSP.type == GeodesicTracer::SurfacePoint::Type::VERTEX &&
        startSP.elementId >= inputVertices.size()) {
        //std::cout << "[traceIntrinsicHalfedge] Start vertex " << intrinsicStartV << " maps to invalid input vertex " << startSP.elementId << " (input mesh size=" << inputVertices.size() << ")" << std::endl;
        return {};
    }
    GeodesicTracer::SurfacePoint endSP = itEnd->second;

    // Get intrinsic vector
    glm::dvec2 traceVec = intrinsicMesh.halfedgeVector(intrinsicHalfedgeIdx);
    double traceLen = glm::length(traceVec);

    if (traceLen < 1e-12) {
        GeodesicTracer::GeodesicTraceResult result;
        result.success = true;
        result.pathPoints = { startSP, endSP };
        result.distance = 0.0;
        
        // Add a step with zero distance
        GeodesicTracer::FaceStepResult step;
        step.success = true;
        step.distanceTraveled = 0.0;
        result.steps.push_back(step);
        
        return result;
    }

    glm::dvec2 traceDir = traceVec / traceLen;

    GeodesicTracer::GeodesicTraceResult base;
    base.success = true;
    base.pathPoints.clear();
    base.pathPoints.push_back(startSP);
    base.distance = 0.0;

    // DEBUG: Print signpost angles for start vertex
    uint32_t startVertex = startSP.elementId;
    if (startSP.type == GeodesicTracer::SurfacePoint::Type::EDGE) {
        // For edge tracing, get the start vertex from the intrinsic halfedge
        auto& meshConn = intrinsicMesh.getConnectivity();
        const auto& halfEdge = meshConn.getHalfEdges()[intrinsicHalfedgeIdx];
        startVertex = halfEdge.origin;
    }

    auto& meshConn = intrinsicMesh.getConnectivity();
    auto outgoingHalfedges = meshConn.getVertexHalfEdges(startVertex);

    for (uint32_t he : outgoingHalfedges) {
        if (he == HalfEdgeMesh::INVALID_INDEX) 
            continue;

        const auto& heData = meshConn.getHalfEdges()[he];
        uint32_t endVertex = heData.next != HalfEdgeMesh::INVALID_INDEX ?
            meshConn.getHalfEdges()[heData.next].origin : HalfEdgeMesh::INVALID_INDEX;
    }

    // Trace on input mesh based on start point type
    GeodesicTracer::GeodesicTraceResult result;

    if (startSP.type == GeodesicTracer::SurfacePoint::Type::VERTEX) {
        // Pick a reference face adjacent to the start vertex. Prioritize the stored resolution face.
        uint32_t refFace = HalfEdgeMesh::INVALID_INDEX;
        auto it = vertexResolutionFaces.find(intrinsicStartV);
        if (it != vertexResolutionFaces.end()) {
            refFace = it->second;
        }

        // Fallback to finding any adjacent face if no resolution face is stored
        if (refFace == HalfEdgeMesh::INVALID_INDEX) {
            auto& inputConn = inputMesh.getConnectivity();
            auto outgoingHalfedges = inputConn.getVertexHalfEdges(startSP.elementId);
            for (uint32_t he : outgoingHalfedges) {
                if (he == HalfEdgeMesh::INVALID_INDEX)
                    continue;
                uint32_t f = inputConn.getHalfEdges()[he].face;
                if (f != HalfEdgeMesh::INVALID_INDEX) {
                    refFace = f;
                    break;
                }
            }
        }

        result = tracerInput.traceFromVertex(startSP.elementId, refFace, traceDir, traceLen, base, traceLen);
    }
    else if (startSP.type == GeodesicTracer::SurfacePoint::Type::FACE) {
        result = tracerInput.traceFromFace(startSP.elementId, startSP.baryCoords, traceDir, traceLen);
    }
    else if (startSP.type == GeodesicTracer::SurfacePoint::Type::EDGE) {

        // Get the resolution face for the start vertex (which face's chart the direction is expressed in)
        uint32_t resolutionFace = HalfEdgeMesh::INVALID_INDEX;
        auto it = vertexResolutionFaces.find(startVertex);
        if (it != vertexResolutionFaces.end()) {
            resolutionFace = it->second;
            //std::cout << "[traceIntrinsicHalfedge] Found resolution face=" << resolutionFace << " for start vertex " << startVertex << std::endl;
        }
        else {
            //std::cout << "[traceIntrinsicHalfedge] WARNING: No resolution face stored for vertex " << startVertex << std::endl;
        }

        result = tracerInput.traceFromEdge(startSP.elementId, startSP.split, traceDir, traceLen, intrinsicHalfedgeIdx, resolutionFace);
    }

    if (result.success) {
        return result;
    }
    else {
        //std::cout << "[traceIntrinsicHalfedge] tracer failed, returning endpoints\n";
        GeodesicTracer::GeodesicTraceResult fallback;
        fallback.success = false;
        fallback.pathPoints = { startSP, endSP };
        fallback.distance = traceLen;
        return fallback;
    }
}

std::vector<glm::vec3> iODT::getCommonSubdivision(uint32_t intrinsicHalfedgeIdx) const {
    std::vector<glm::vec3> polyline;

    // Trace the intrinsic halfedge to get surface points
    auto traceResult = const_cast<iODT*>(this)->traceIntrinsicHalfedgeAlongInput(intrinsicHalfedgeIdx);

    // Convert surface points to 3D positions
    for (const auto& sp : traceResult.pathPoints) {
        glm::dvec3 pos3D = tracerInput.evaluateSurfacePoint(sp);
        polyline.push_back(glm::vec3(pos3D));
    }

    return polyline;
}

void iODT::saveCommonSubdivisionOBJ(const std::string& filename, const Model& overlayModel) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[saveIntrinsicOverlayOBJ] Failed to open file: " << filename << std::endl;
        return;
    }

    // Extract vertices from the overlay model
    const auto& vertices = overlayModel.getVertices();
    const auto& indices = overlayModel.getIndices();

    // Write vertices
    for (const auto& vertex : vertices) {
        file << "v " << vertex.pos.x << " " << vertex.pos.y << " " << vertex.pos.z << "\n";
    }

    // Write vertex colors
    for (const auto& vertex : vertices) {
        file << "vc " << vertex.color.r << " " << vertex.color.g << " " << vertex.color.b << "\n";
    }

    // Write face elements 
    uint32_t faceCount = 0;
    for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 < indices.size()) {
            // OBJ uses 1-based indexing
            file << "f " << (indices[i] + 1) << " " << (indices[i + 1] + 1) << " " << (indices[i + 2] + 1) << "\n";
            faceCount++;
        }
    }

    file.close();
    std::cout << "[saveIntrinsicOverlayOBJ] Saved " << vertices.size() << " vertices and " << faceCount << " faces to " << filename << std::endl;
}

void iODT::createCommonSubdivision(Model& overlayModel, std::vector<CommonSubdivision::IntrinsicTriangle>& outIntrinsicTriangles) {
    commonSubdivision = std::make_unique<CommonSubdivision>(
        intrinsicMesh,
        inputMesh,
        tracerInput,
        intrinsicVertexLocations,
        [this](uint32_t halfedgeIdx) -> std::vector<GeodesicTracer::SurfacePoint> {
            auto traceResult = this->traceIntrinsicHalfedgeAlongInput(halfedgeIdx);
            return traceResult.pathPoints;
        }
    );
    
    commonSubdivision->build();
    
    outIntrinsicTriangles = commonSubdivision->getIntrinsicTriangles();
    
    commonSubdivision->exportToModel(overlayModel);
}

void iODT::cleanup() {
    if (supportingHalfedge) {
        supportingHalfedge->cleanup();
    }

    supportingHalfedge.reset();
    commonSubdivision.reset();
}
