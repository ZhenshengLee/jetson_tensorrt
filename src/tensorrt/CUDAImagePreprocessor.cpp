/**
 * @file	CUDAImagePreprocessor.cpp
 * @author	Carroll Vance
 * @brief	Abstract class representing a CUDA accelerated image preprocessor
 *
 * Copyright (c) 2018 Carroll Vance.
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <string>
#include <cstddef>
#include <stdexcept>

#include <cuda_runtime.h>
#include <cuda.h>

#include "CUDAImagePreprocessor.h"
#include "RTCommon.h"


namespace jetson_tensorrt {

CUDAImagePreprocessor::CUDAImagePreprocessor() {
	inputCache = new CUDASizedMemCache();
	outputCache = new CUDASizedMemCache();
}
CUDAImagePreprocessor::~CUDAImagePreprocessor() {
	delete inputCache;
	delete outputCache;
}

void CUDAImagePreprocessor::inputFromHost(void* hostMemory, size_t size) {
	void* deviceMemory = inputCache->getCUDAAlloc(size);

	cudaError_t hostDeviceError = cudaMemcpy(deviceMemory, hostMemory, size,
			cudaMemcpyHostToDevice);

	if (hostDeviceError != 0)
		throw std::runtime_error(
				"Unable to copy host memory to device for preprocessing. CUDA Error: "
						+ std::to_string(hostDeviceError));


}

void CUDAImagePreprocessor::swapIO(){
	CUDASizedMemCache* tmp = outputCache;

	outputCache = inputCache;
	inputCache = tmp;
}

} /* namespace jetson_tensorrt */
