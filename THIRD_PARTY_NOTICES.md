# Third-Party Notices

WEXYRON is distributed under the [MIT License](LICENSE). This file lists
the third-party components it bundles or links against. Their licenses apply
to those components only and do not change the MIT license of the project's
own code.

## GLFW 3.4

Location: `.mingw-deps/glfw/`, `glfw3.dll` in Windows builds
License: zlib/libpng

```text
Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Löwy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
```

## miniaudio

Location: `third_party/miniaudio/`
License: public domain (Unlicense) or MIT No Attribution (MIT-0), at your option

Copyright 2026 David Reid. The full dual-license text is at the end of
`third_party/miniaudio/miniaudio.h`.

## stb_image v2.30

Location: `include/stb/stb_image.h`
License: public domain or MIT, at your option

```text
Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## glad 0.1.36

Location: `glad.c`, `include/glad/`
License: MIT

Generated OpenGL loader. See <https://github.com/Dav1dde/glad>.

## Khronos khrplatform.h

Location: `include/KHR/khrplatform.h`
License: MIT-style Khronos license

Copyright (c) 2008-2018 The Khronos Group Inc. The full license text is in the
header.

## GLM

License: MIT

Header-only math library used at build time. It is not redistributed as a
binary component. See <https://github.com/g-truc/glm>.

## Mesa 3D (Windows software-renderer fallback)

Location: `mesa-deps/x64/` (`opengl32.dll`, `libgallium_wgl.dll`,
`libdlclose-skip.dll`)
License: MIT

See <https://docs.mesa3d.org/license.html>.

## NVRHI

Location: `third_party/nvrhi/` (git submodule, NVIDIA-RTX/NVRHI)
License: MIT

Copyright (c) 2014-2021, NVIDIA CORPORATION. See `third_party/nvrhi/LICENSE.txt`.

## Vulkan-Headers

Location: `third_party/vulkan-headers/` (git submodule, KhronosGroup/Vulkan-Headers)
License: Apache-2.0 OR MIT

Copyright 2015-2023 The Khronos Group Inc. See `third_party/vulkan-headers/LICENSE.md`.

## AMD FidelityFX / FSR SDK (frame generation, Windows)

Files: `amd_fidelityfx_loader_vk.dll`, `amd_fidelityfx_framegeneration_vk.dll`
(fetched by `tools/fetch_sdks.py` from GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
v2.4.0; not stored in this repository)
License: AMD FidelityFX SDK licence (redistribution in binary form only)

Copyright (C) Advanced Micro Devices, Inc. All rights reserved. The licence
permits installing, reproducing and distributing the software in binary form
only, prohibits reverse engineering, decompilation and disassembly, and
provides the software "AS IS" without warranty of any kind; AMD and its
licensors are not liable for any damages arising from its use. The complete
licence text, including all disclaimers, ships next to the DLLs as
`AMD_FidelityFX_LICENSE.md`.

## MinGW-w64 runtime libraries

Location: `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll` in
Windows builds

- `libgcc_s_seh-1.dll` and `libstdc++-6.dll` are distributed under the GNU
  General Public License v3 or later **with the GCC Runtime Library
  Exception**. The exception permits distributing them with an independent
  program without applying the GPL to that program.
- `libwinpthread-1.dll` is distributed under a permissive MIT/BSD-style
  license.

See <https://www.gnu.org/licenses/gcc-exception-3.1.html>.

## Discord RPC (discord-rpc)

Location: `third_party/discord-rpc/`
License: MIT

```text
Copyright 2017 Discord, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## RapidJSON

Location: `third_party/rapidjson/`
License: MIT (with the third-party notices reproduced below)

Header-only JSON library used by discord-rpc.

```text
Tencent is pleased to support the open source community by making RapidJSON available. 
 
Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip.  All rights reserved.

If you have downloaded a copy of the RapidJSON binary from Tencent, please note that the RapidJSON binary is licensed under the MIT License.
If you have downloaded a copy of the RapidJSON source code from Tencent, please note that RapidJSON source code is licensed under the MIT License, except for the third-party components listed below which are subject to different license terms.  Your integration of RapidJSON into your own projects may require compliance with the MIT License, as well as the other licenses applicable to the third-party components included within RapidJSON. To avoid the problematic JSON license in your own projects, it's sufficient to exclude the bin/jsonchecker/ directory, as it's the only code under the JSON license.
A copy of the MIT License is included in this file.

Other dependencies and licenses:

Open Source Software Licensed Under the BSD License:
--------------------------------------------------------------------

The msinttypes r29 
Copyright (c) 2006-2013 Alexander Chemeris 
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. 
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
* Neither the name of  copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Open Source Software Licensed Under the JSON License:
--------------------------------------------------------------------

json.org 
Copyright (c) 2002 JSON.org
All Rights Reserved.

JSON_checker
Copyright (c) 2002 JSON.org
All Rights Reserved.

	
Terms of the JSON License:
---------------------------------------------------

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

The Software shall be used for Good, not Evil.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


Terms of the MIT License:
--------------------------------------------------------------------

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Assets

The textures and audio files in `assets/` are generated by the project's own
scripts (`tools/generate_textures.py` and `tools/generate_sounds.py`) and are
covered by the project's MIT license.

## SpatialKit

Location: `third_party/spatialkit/`

A vendored header snapshot from the author's SpatialKit project, covered by
the MIT license of this project.
