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

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe voronoi_surface.geom -o voronoi_surface_geom.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe voronoi_surface.vert -o voronoi_surface_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe voronoi_surface.frag -o voronoi_surface_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe gizmo.vert -o gizmo_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe gizmo.frag -o gizmo_frag.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe pick_model.vert -o pick_model_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe pick_model.frag -o pick_model_frag.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe pick_gizmo.frag -o pick_gizmo_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe point_cloud.vert -o point_cloud_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe point_cloud.frag -o point_cloud_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe heat_contact_levelset.vert -o heat_contact_levelset_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe heat_contact_levelset.frag -o heat_contact_levelset_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe vector_arrow.vert -o vector_arrow_vert.spv
C:/VulkanSDK/1.3.283.0/Bin/glslc.exe vector_arrow.frag -o vector_arrow_frag.spv

C:/VulkanSDK/1.3.283.0/Bin/glslc.exe --target-env=vulkan1.3 hash_grid_build.comp -o hash_grid_build_comp.spv

slangc heat_surface_temp.slang -target spirv -o heat_surface_temp_comp.spv
slangc heat_surface_gradient.slang -target spirv -o heat_surface_gradient_comp.spv

slangc heat_diffusion.slang -target spirv -o heat_diffusion_comp.spv

slangc voronoi_geometry.slang -target spirv -o voronoi_geometry_comp.spv
slangc voronoi_candidates.slang -target spirv -o voronoi_candidates_comp.spv
slangc lloyd_accumulate.slang -target spirv -o lloyd_accumulate_comp.spv
slangc lloyd_update.slang -target spirv -o lloyd_update_comp.spv

slangc ibl_equirect_to_cube.slang -target spirv -o ibl_equirect_to_cube_comp.spv
slangc ibl_irradiance.slang -target spirv -o ibl_irradiance_comp.spv
slangc ibl_prefilter.slang -target spirv -o ibl_prefilter_comp.spv
slangc ibl_brdf_lut.slang -target spirv -o ibl_brdf_lut_comp.spv

REM QRhi viewport blit shaders 
C:/Qt/6.9.3/msvc2022_64/bin/qsb.exe --glsl "100 es,120,150" --hlsl 50 --msl 12 -o viewport_blit.vert.qsb viewport_blit.vert
C:/Qt/6.9.3/msvc2022_64/bin/qsb.exe --glsl "100 es,120,150" --hlsl 50 --msl 12 -o viewport_blit.frag.qsb viewport_blit.frag


popd
pause
