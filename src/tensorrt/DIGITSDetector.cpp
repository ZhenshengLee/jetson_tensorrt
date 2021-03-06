/**
 * @file	DIGITSDetector.cpp
 * @author	Carroll Vance
 * @brief	Loads and manages a DIGITS DetectNet graph with TensorRT
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

#include <fstream>

#include "DIGITSDetector.h"

namespace jetson_tensorrt {

const std::string DIGITSDetector::INPUT_NAME = "data";
const std::string DIGITSDetector::OUTPUT_COVERAGE_NAME = "coverage";
const std::string DIGITSDetector::OUTPUT_BBOXES_NAME = "bboxes";

DIGITSDetector::DIGITSDetector(std::string prototextPath, std::string modelPath,
                               std::string cachePath, size_t nbChannels,
                               size_t width, size_t height, size_t stride,
                               size_t nbClasses, nvinfer1::DataType dataType,
                               size_t maxNetworkSize)
    : CaffeRTEngine() {

  if (nbChannels != CHANNELS_BGR)
    throw std::invalid_argument("Only BGR DetectNets are supported currently");

  addInput(INPUT_NAME, nvinfer1::DimsCHW(nbChannels, height, width),
           sizeof(float));

  nvinfer1::Dims outputDimsCoverage;
  outputDimsCoverage.nbDims = 3;
  outputDimsCoverage.d[0] = nbClasses;
  outputDimsCoverage.d[1] = (size_t)(height / stride);
  outputDimsCoverage.d[2] = (size_t)(width / stride);
  addOutput(OUTPUT_COVERAGE_NAME, outputDimsCoverage, sizeof(float));

  nvinfer1::Dims outputDimsBboxes;
  outputDimsBboxes.nbDims = 3;
  outputDimsBboxes.d[0] = 4;
  outputDimsBboxes.d[1] = (size_t)(height / stride);
  outputDimsBboxes.d[2] = (size_t)(width / stride);
  addOutput(OUTPUT_BBOXES_NAME, outputDimsBboxes, sizeof(float));

  std::ifstream infile(cachePath);
  if (infile.good()) {
    loadCache(cachePath);
  } else {
    loadModel(prototextPath, modelPath, 1, dataType, maxNetworkSize);
    saveCache(cachePath);
  }

  this->modelWidth = width;
  this->modelHeight = height;
  this->modelDepth = nbChannels;
  this->nbClasses = nbClasses;

  // Configure non-maximum suppression based on what we currently know
  suppressor.setupInput(width, height);
  suppressor.setupGrid((size_t)(width / stride), (size_t)(height / stride));
}

DIGITSDetector::~DIGITSDetector() {}

std::vector<RTClassifiedRegionOfInterest>
DIGITSDetector::detect(LocatedExecutionMemory &inputs,
                       LocatedExecutionMemory &outputs, float threshold) {

  // Execute inference
  predict(inputs, outputs);

  float *coverage = (float *)outputs.batch[0][0];
  float *bboxes = (float *)outputs.batch[0][1];

  // Configure non-maximum suppressor for the current image size
  // TODO: convert bounding rects back to original image scale
  suppressor.setupImage(modelWidth, modelHeight);

  return suppressor.execute(coverage, bboxes, nbClasses, threshold);
}

ClusteredNonMaximumSuppression::ClusteredNonMaximumSuppression() {

  imageDimX = 0;
  imageDimY = 0;
  imageScaleX = 0.0;
  imageScaleY = 0.0;
  inputDimX = 0;
  inputDimY = 0;
  gridDimX = 0;
  gridDimY = 0;
  cellWidth = 0;
  cellHeight = 0;
  gridSize = 0;

  imageReady = false;
  inputReady = false;
  gridReady = false;
}

ClusteredNonMaximumSuppression::~ClusteredNonMaximumSuppression() {}

void ClusteredNonMaximumSuppression::setupImage(size_t imageDimX,
                                                size_t imageDimY) {
  this->imageDimX = imageDimX;
  this->imageDimY = imageDimY;

  imageReady = true;

  if (imageReady && inputReady && gridReady)
    calculateScale();
}

void ClusteredNonMaximumSuppression::setupInput(size_t inputDimX,
                                                size_t inputDimY) {
  this->inputDimX = inputDimX;
  this->inputDimY = inputDimY;

  inputReady = true;

  if (imageReady && inputReady && gridReady)
    calculateScale();
}

void ClusteredNonMaximumSuppression::setupGrid(size_t gridDimX,
                                               size_t gridDimY) {
  this->gridDimX = gridDimX;
  this->gridDimY = gridDimY;
  this->gridSize = gridDimX * gridDimY;

  gridReady = true;

  if (imageReady && inputReady && gridReady)
    calculateScale();
}

void ClusteredNonMaximumSuppression::calculateScale() {
  imageScaleX = inputDimX / imageDimX;
  imageScaleY = inputDimY / imageDimY;

  cellWidth = inputDimX / gridDimX;
  cellHeight = inputDimY / gridDimY;
}

// TODO: Refactor all of this code to use names that make sense for the task at
// hand
struct float6 {
  float x;
  float y;
  float z;
  float w;
  float v;
  float u;
};
static inline float6 make_float6(float x, float y, float z, float w, float v,
                                 float u) {
  float6 f;
  f.x = x;
  f.y = y;
  f.z = z;
  f.w = w;
  f.v = v;
  f.u = u;
  return f;
}

// TODO: Refactor all of this code to use names that make sense for the task at
// hand
inline static bool rectOverlap(const float6 &r1, const float6 &r2) {
  return !(r2.x > r1.z || r2.z < r1.x || r2.y > r1.w || r2.w < r1.y);
}

// TODO: Refactor all of this code to use names that make sense for the task at
// hand
static void mergeRect(std::vector<float6> &rects, const float6 &rect) {
  const size_t num_rects = rects.size();

  bool intersects = false;

  for (size_t r = 0; r < num_rects; r++) {
    if (rectOverlap(rects[r], rect)) {
      intersects = true;

      if (rect.x < rects[r].x)
        rects[r].x = rect.x;
      if (rect.y < rects[r].y)
        rects[r].y = rect.y;
      if (rect.z > rects[r].z)
        rects[r].z = rect.z;
      if (rect.w > rects[r].w)
        rects[r].w = rect.w;

      break;
    }
  }

  if (!intersects)
    rects.push_back(rect);
}

std::vector<RTClassifiedRegionOfInterest>
ClusteredNonMaximumSuppression::execute(float *coverage, float *bboxes,
                                        size_t nbClasses,
                                        float coverageThreshold) {

  // Cluster the rects
  std::vector<std::vector<float6>> rects;
  rects.resize(nbClasses);

  for (int c = 0; c < nbClasses; c++) {

    rects[c].reserve(gridSize);

    for (int y = 0; y < gridDimY; y++) {
      for (int x = 0; x < gridDimX; x++) {

        const float cvg = coverage[c * gridSize + y * gridDimX + x];

        if (cvg > coverageThreshold) {
          const float mx = x * cellWidth;
          const float my = y * cellHeight;

          const float x1 = (bboxes[0 * gridSize + y * gridDimX + x] + mx) *
                           imageScaleX; // left
          const float y1 = (bboxes[1 * gridSize + y * gridDimX + x] + my) *
                           imageScaleY; // top
          const float x2 = (bboxes[2 * gridSize + y * gridDimX + x] + mx) *
                           imageScaleX; // right
          const float y2 = (bboxes[3 * gridSize + y * gridDimX + x] + my) *
                           imageScaleY; // bottom

          mergeRect(rects[c], make_float6(x1, y1, x2, y2, cvg, c));
        }
      }
    }
  }

  std::vector<RTClassifiedRegionOfInterest> classRectangles;

  const size_t maxBoundingBoxes = gridSize * nbClasses;
  int boxIndex = 0;

  for (size_t c = 0; c < nbClasses; c++) {
    const size_t numBox = rects[c].size();

    for (size_t b = 0; b < numBox && boxIndex < maxBoundingBoxes; b++) {

      const float6 r = rects[c][b];

      const int classID = (size_t)r.u;
      const float coverage = r.v;

      const size_t x1 = (size_t)r.x;
      const size_t y1 = (size_t)r.y;
      const size_t x2 = (size_t)r.z;
      const size_t y2 = (size_t)r.w;

      classRectangles.push_back(RTClassifiedRegionOfInterest(
          classID, coverage, x1, y1, x2 - x1, y2 - y1));

      boxIndex++;
    }
  }

  return classRectangles;
}

} /* namespace jetson_tensorrt */
