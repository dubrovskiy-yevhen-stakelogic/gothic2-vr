# Indexed indirect Vulkan test

The Tempest API supports empty-VBO indexed draws (shader vertex pulling):

- `drawIndexedIndirect(ibo, commands, byteOffset, drawCount=1, byteStride=20)`
- `drawIndexedIndirectCount(ibo, commands, byteOffset, countBuffer, countByteOffset, maxDrawCount, byteStride=20)`

Both use `Tempest::DrawIndexedIndirectCommand` (20 bytes, Vulkan layout). The Vulkan backend uses one native call, including multi-draw. It never expands meshlets into a CPU draw loop. Unsupported backends default to `UnsupportedExtension`; callers must check `Device::properties().indirect` before choosing this path.

Capabilities: `indexed`, `multiDraw`, `count`, `firstInstance`, `maxDrawCount`. Vulkan explicitly enables supported multiDrawIndirect and drawIndirectFirstInstance base features. Count support requires `VK_KHR_draw_indirect_count` and a loaded function pointer. The extension enables its feature without adding the Vulkan 1.2 feature aggregate, which would conflict with existing extension feature structs. This implementation deliberately does not advertise core-only count support on devices lacking the extension.

API checks validate rendering state, buffer presence, byte alignment, command stride, storage bounds and integer overflow. Vulkan checks draw-count limits and enabled capability. Indirect command contents live on the GPU: caller must ensure valid index ranges and firstInstance=0 on devices without firstInstance support, and ensure the count-buffer value never exceeds maxDrawIndirectCount.

GPU tests render without a window or game assets. They verify 32,256 analytically predicted pixels across twelve full image comparisons, plus a single-offset test for each index width. Covered behavior: 16/32-bit index buffers, a single command with two instances, firstIndex=3, vertexOffset=5, firstInstance=7/8, nonzero command/count offsets, padded stride=32, GPU-written commands/count, count zero/one/two and clamping three to two, multi-draw, command-buffer reuse, and invalid API arguments. A third blue draw would visibly fail the count-clamp assertion if executed. All shader binaries pass spirv-val for Vulkan 1.0.

Current host run: NVIDIA RTX 4090, all required capabilities available, PASS. Vulkan validation layers are unavailable on the host, so this is execution/pixel verification rather than a validation-layer result. Captured Quest vkjson separately confirms Adreno 740 Vulkan 1.3.295, multiDrawIndirect=1, drawIndirectFirstInstance=1, drawIndirectCount=1, the KHR extension, and maxDrawIndirectCount=4294967295; this is a capability query, not Quest render validation.

Configure this directory as a standalone CMake project. `TEMPEST_TEST_LIBRARY` may point to the consuming build's rebuilt Tempest import library. Provide `GLSLC_EXECUTABLE`. Run the executable with the build directory as working directory and the corresponding rebuilt Tempest DLL on PATH (the public properties structure changed). In this workspace the host build must set VULKAN_SDK to sdkRoot from toolchain/desktop-toolchain-manifest.json before CMake configure or build.

Evidence: build/tests/indexed-indirect/direct-vulkan.log, quest-vkjson.json, source-manifest.json, tempest-build.log. No game was launched and no APK was built or installed by this test.

Vulkan references:
- https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirectCount.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html (VUID 02831, extension enabling)
