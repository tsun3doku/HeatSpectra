@echo off
pushd "%~dp0"
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe grid.vert -o grid_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe grid.frag -o grid_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe grid_label.vert -o grid_label_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe grid_label.frag -o grid_label_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe gbuffer.vert -o gbuffer_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe gbuffer.frag -o gbuffer_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe wireframe.vert -o wireframe_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe wireframe.frag -o wireframe_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_common.vert -o intrinsic_common_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_common.frag -o intrinsic_common_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_supporting.vert -o intrinsic_supporting_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_supporting.geom -o intrinsic_supporting_geom.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_supporting.frag -o intrinsic_supporting_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_normals.vert -o intrinsic_normals_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_normals.geom -o intrinsic_normals_geom.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_normals.frag -o intrinsic_normals_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_vertex_normals.vert -o intrinsic_vertex_normals_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_vertex_normals.geom -o intrinsic_vertex_normals_geom.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe intrinsic_vertex_normals.frag -o intrinsic_vertex_normals_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe heat_buffer.frag -o heat_buffer_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe lighting.vert -o lighting_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe lighting.frag -o lighting_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe blend.vert -o blend_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe blend.frag -o blend_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe outline.vert -o outline_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe outline.frag -o outline_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe hash_grid_vis.vert -o hash_grid_vis_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe hash_grid_vis.frag -o hash_grid_vis_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe surfel_debug.vert -o surfel_debug_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe surfel_debug.frag -o surfel_debug_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe voronoi_surface.vert -o voronoi_surface_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe voronoi_surface.frag -o voronoi_surface_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe gizmo.vert -o gizmo_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe gizmo.frag -o gizmo_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe point_cloud.vert -o point_cloud_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe point_cloud.frag -o point_cloud_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe contact_lines.vert -o contact_lines_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe contact_lines.frag -o contact_lines_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe --target-env=vulkan1.3 hash_grid_build.comp -o hash_grid_build_comp.spv

slangc heat_surface.slang -target spirv -o heat_surface_comp.spv

slangc heat_contact.slang -target spirv -o heat_contact_comp.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe --target-env=vulkan1.3 heat_source.comp -o heat_source_comp.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe --target-env=vulkan1.3 heat_voronoi.comp -o heat_voronoi_comp.spv

slangc heat_geometry.slang -target spirv -o heat_geometry_comp.spv

slangc lloyd_accumulate.slang -target spirv -o lloyd_accumulate_comp.spv

slangc lloyd_update.slang -target spirv -o lloyd_update_comp.spv




popd
pause