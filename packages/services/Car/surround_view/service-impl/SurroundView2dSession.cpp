/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SurroundView2dSession.h"

#include <android-base/logging.h>
#include <android/hardware_buffer.h>
#include <system/camera_metadata.h>
#include <utils/SystemClock.h>
#include <cutils/properties.h>

#include <thread>

#include <android/hardware/camera/device/3.2/ICameraDevice.h>

#include "CameraUtils.h"

using ::std::adopt_lock;
using ::std::lock;
using ::std::lock_guard;
using ::std::map;
using ::std::mutex;
using ::std::scoped_lock;
using ::std::string;
using ::std::thread;
using ::std::unique_lock;
using ::std::unique_ptr;
using ::std::vector;

using ::android::hardware::automotive::evs::V1_0::EvsResult;
using ::android::hardware::camera::device::V3_2::Stream;
using namespace imx;

using GraphicsPixelFormat = ::android::hardware::graphics::common::V1_0::PixelFormat;

namespace android {
namespace hardware {
namespace automotive {
namespace sv {
namespace V1_0 {
namespace implementation {

// TODO(b/158479099): There are a lot of redundant code between 2d and 3d.
// Decrease the degree of redundancy.
typedef struct {
    int32_t id;
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t direction;
    int32_t framerate;
} RawStreamConfig;

static const size_t kStreamCfgSz = sizeof(RawStreamConfig) / sizeof(int32_t);
static const uint8_t kGrayColor = 128;
static const int kInputNumChannels = 3;
static const int kOutputNumChannels = 3;
static const int kNumFrames = 4;
static const int kSv2dViewId = 0;

// #define DEBUG_SURROUNDVIEW_2D
#ifdef DEBUG_SURROUNDVIEW_2D
static int nu=0;
#endif

SurroundView2dSession::FramesHandler::FramesHandler(
    sp<IEvsCamera> pCamera, sp<SurroundView2dSession> pSession)
    : mCamera(pCamera),
      mSession(pSession) {}

Return<void> SurroundView2dSession::FramesHandler::deliverFrame(
    const BufferDesc_1_0& bufDesc_1_0) {
    LOG(INFO) << "Ignores a frame delivered from v1.0 EVS service.";
    mCamera->doneWithFrame(bufDesc_1_0);

    return {};
}

Return<void> SurroundView2dSession::dump_frame_to_file(char *pbuf, int size, char *filename)
{
    int fd = 0;
    int len = 0;
    fd = open(filename, O_CREAT | O_RDWR, 0666);

    if (fd<0) {
        LOG(ERROR) << "failed to open";
    }
    len = write(fd, pbuf, size);
    if (len < 0)
        LOG(ERROR) << "failed to write file";
    close(fd);
    return {};
}

Return<void> SurroundView2dSession::FramesHandler::deliverFrame_1_1(
    const hidl_vec<BufferDesc_1_1>& buffers) {
    LOG(INFO) << "Received " << buffers.size() << " frames from the camera";
    mSession->mSequenceId++;

    {
        scoped_lock<mutex> lock(mSession->mAccessLock);
        if (mSession->mProcessingEvsFrames) {
            LOG(WARNING) << "EVS frames are being processed. Skip frames:"
                         << mSession->mSequenceId;
            mCamera->doneWithFrame_1_1(buffers);
            return {};
        } else {
            // Sets the flag to true immediately so the new coming frames will
            // be skipped.
            mSession->mProcessingEvsFrames = true;
        }
    }

    if (buffers.size() != kNumFrames) {
        scoped_lock<mutex> lock(mSession->mAccessLock);
        LOG(ERROR) << "The number of incoming frames is " << buffers.size()
                   << ", which is different from the number " << kNumFrames
                   << ", specified in config file";
        mSession->mProcessingEvsFrames = false;
        mCamera->doneWithFrame_1_1(buffers);
        return {};
    }

    {
        scoped_lock<mutex> lock(mSession->mAccessLock);

        if (mSession->mIOModuleConfig->cameraConfig.evsCameraIds.size() != kNumFrames) {
            LOG(ERROR) << "The frames are not from the cameras we expected!";
            mSession->mProcessingEvsFrames = false;
            mCamera->doneWithFrame_1_1(buffers);
            return {};
        }

        for (int i = 0; i < kNumFrames; i++) {
            LOG(DEBUG) << "Copying buffer from camera ["
                       << buffers[i].deviceId
                       << "] to Surround View Service";
            mSession->copyFromBufferToPointers(buffers[i],
                                               mSession->mInputPointers[i]);
        }
    }

#ifdef DEBUG_SURROUNDVIEW_2D
    nu++;
#endif

    // if the format of some buffer are not RGB888, return the buffer directly.
    // if the format of all buffer are RGB888, return the buffer until handle
    // the frame(convert to the outbuffer)
    if (!mSession->mHandleFrameDirect)
        mCamera->doneWithFrame_1_1(buffers);

    // Notify the session that a new set of frames is ready
    mSession->mFramesSignal.notify_all();

    if (mSession->mHandleFrameDirect) {
        {
            unique_lock<mutex> lock(mSession->mAccessLock);
            mSession->mFramesSignal.wait(lock, [this]() { return !mSession->mProcessingEvsFrames; });
        }

        mCamera->doneWithFrame_1_1(buffers);
    }

    return {};
}

Return<void> SurroundView2dSession::FramesHandler::notify(const EvsEventDesc& event) {
    switch(event.aType) {
        case EvsEventType::STREAM_STOPPED:
            // The Surround View STREAM_STOPPED event is generated when the
            // service finished processing the queued frames. So it does not
            // rely on the Evs STREAM_STOPPED event.
            LOG(INFO) << "Received a STREAM_STOPPED event from Evs.";
            break;

        case EvsEventType::PARAMETER_CHANGED:
            LOG(INFO) << "Camera parameter " << std::hex << event.payload[0]
                      << " is set to " << event.payload[1];
            break;

        // Below events are ignored in reference implementation.
        case EvsEventType::STREAM_STARTED:
        [[fallthrough]];
        case EvsEventType::FRAME_DROPPED:
        [[fallthrough]];
        case EvsEventType::TIMEOUT:
            LOG(INFO) << "Event " << std::hex << static_cast<unsigned>(event.aType)
                      << "is received but ignored.";
            break;
        default:
            LOG(ERROR) << "Unknown event id: " << static_cast<unsigned>(event.aType);
            break;
    }

    return {};
}

bool SurroundView2dSession::copyFromBufferToPointers(
    BufferDesc_1_1 buffer, SurroundViewInputBufferPointers& pointers) {

    AHardwareBuffer_Desc* pDesc =
        reinterpret_cast<AHardwareBuffer_Desc *>(&buffer.buffer.description);

    // create a GraphicBuffer from the existing handle
    sp<GraphicBuffer> inputBuffer = new GraphicBuffer(
        buffer.buffer.nativeHandle, GraphicBuffer::CLONE_HANDLE, pDesc->width,
        pDesc->height, pDesc->format, pDesc->layers,
        GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_OFTEN |
        GRALLOC_USAGE_SW_WRITE_OFTEN, pDesc->stride);

    if (inputBuffer == nullptr) {
        LOG(ERROR) << "Failed to allocate GraphicBuffer to wrap image handle";
        // Returning "true" in this error condition because we already released the
        // previous image (if any) and so the texture may change in unpredictable
        // ways now!
        return false;
    } else {
        LOG(INFO) << "Managed to allocate GraphicBuffer with "
                  << " width: " << pDesc->width
                  << " height: " << pDesc->height
                  << " format: " << pDesc->format
                  << " stride: " << pDesc->stride;
    }

    // Lock the input GraphicBuffer and map it to a pointer.  If we failed to
    // lock, return false.
    void* inputDataPtr;
    inputBuffer->lock(
        GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN,
        &inputDataPtr);
    if (!inputDataPtr) {
        LOG(ERROR) << "Failed to gain read access to GraphicBuffer";
        inputBuffer->unlock();
        return false;
    } else {
        LOG(INFO) << "Managed to get read access to GraphicBuffer";
    }

    int stride = pDesc->stride;

    // readPtr comes from EVS, and it is with 4 channels
    uint8_t* readPtr = static_cast<uint8_t*>(inputDataPtr);

    // writePtr comes from CV imread, and it is with 3 channels
    uint8_t* writePtr = static_cast<uint8_t*>(pointers.cpu_data_pointer);

    if (pDesc->format == HAL_PIXEL_FORMAT_RGBA_8888) {
        // if the evs buffer is RGBA8888, convert it to RGB888 here
        for (int i=0; i<pDesc->width; i++)
            for (int j=0; j<pDesc->height; j++) {
                writePtr[(i + j * stride) * 3 + 0] =
                    readPtr[(i + j * stride) * 4 + 0];
                writePtr[(i + j * stride) * 3 + 1] =
                    readPtr[(i + j * stride) * 4 + 1];
                writePtr[(i + j * stride) * 3 + 2] =
                    readPtr[(i + j * stride) * 4 + 2];
            }
        mHandleFrameDirect = false;
    } else if (pDesc->format == HAL_PIXEL_FORMAT_RGB_888) {
        // if the evs buffer is RGB888, just set the input point to output point
        // return evs buffer until inputpoint convert to outpoint, if mHandleFrameDirect is true.
        // return evs buffer until evs buffer convert to inputpoint, if mHandleFrameDirect is false.
        memcpy(writePtr, readPtr,  pDesc->width * pDesc->height * 3);
        mHandleFrameDirect = false;
        //pointers.cpu_data_pointer = (void*)readPtr;
        // mHandleFrameDirect = true;
    }

    LOG(INFO) << "Brute force copying finished";
    return true;
}

void SurroundView2dSession::processFrames() {
    while (true) {
        {
            unique_lock<mutex> lock(mAccessLock);

            if (mStreamState != RUNNING) {
                break;
            }

            mFramesSignal.wait(lock, [this]() { return mProcessingEvsFrames; });
        }

        handleFrames(mSequenceId);

        {
            // Set the boolean to false to receive the next set of frames.
            scoped_lock<mutex> lock(mAccessLock);
            mProcessingEvsFrames = false;
        }
        if (mHandleFrameDirect) {
            mFramesSignal.notify_all();
        }
    }

    // Notify the SV client that no new results will be delivered.
    LOG(DEBUG) << "Notify SvEvent::STREAM_STOPPED";
    mStream->notify(SvEvent::STREAM_STOPPED);

    {
        scoped_lock<mutex> lock(mAccessLock);
        mStreamState = STOPPED;
        mStream = nullptr;
        LOG(DEBUG) << "Stream marked STOPPED.";
    }
}

SurroundView2dSession::SurroundView2dSession(sp<IEvsEnumerator> pEvs,
                                             IOModuleConfig* pConfig)
    : mEvs(pEvs),
      mIOModuleConfig(pConfig),
      mStreamState(STOPPED) {
    mEvsCameraIds = {"0", "1", "2", "3"};
}

SurroundView2dSession::~SurroundView2dSession() {
    // In case the client did not call stopStream properly, we should stop the
    // stream explicitly. Otherwise the process thread will take forever to
    // join.
    stopStream();

    // Waiting for the process thread to finish the buffered frames.
    if (mProcessThread.joinable()) {
        mProcessThread.join();
    }

    mEvs->closeCamera(mCamera);
}

// Methods from ::android::hardware::automotive::sv::V1_0::ISurroundViewSession
Return<SvResult> SurroundView2dSession::startStream(
    const sp<ISurroundViewStream>& stream) {
    LOG(DEBUG) << __FUNCTION__;
    scoped_lock<mutex> lock(mAccessLock);

    if (!mIsInitialized && !initialize()) {
        LOG(ERROR) << "There is an error while initializing the use case. "
                   << "Exiting";
        return SvResult::INTERNAL_ERROR;
    }

    if (mStreamState != STOPPED) {
        LOG(ERROR) << "Ignoring startVideoStream call"
                   << "when a stream is already running.";
        return SvResult::INTERNAL_ERROR;
    }

    if (stream == nullptr) {
        LOG(ERROR) << "The input stream is invalid";
        return SvResult::INTERNAL_ERROR;
    }
    mStream = stream;

    mSequenceId = 0;
    startEvs();

    // TODO(b/158131080): the STREAM_STARTED event is not implemented in EVS
    // reference implementation yet. Once implemented, this logic should be
    // moved to EVS notify callback.
    LOG(DEBUG) << "Notify SvEvent::STREAM_STARTED";
    mStream->notify(SvEvent::STREAM_STARTED);
    mProcessingEvsFrames = false;
    mHandleFrameDirect = true;

    // Start the frame generation thread
    mStreamState = RUNNING;

    mProcessThread = thread([this]() {
        processFrames();
    });

    return SvResult::OK;
}

Return<void> SurroundView2dSession::stopStream() {
    LOG(DEBUG) << __FUNCTION__;
    unique_lock<mutex> lock(mAccessLock);

    if (mStreamState == RUNNING) {
        // Tell the processFrames loop to stop processing frames
        mStreamState = STOPPING;

        // Stop the EVS stream asynchronizely
        mCamera->stopVideoStream();
        mFramesHandler = nullptr;
    }

    return {};
}

Return<void> SurroundView2dSession::doneWithFrames(
    const SvFramesDesc& svFramesDesc){
    LOG(DEBUG) << __FUNCTION__;
    scoped_lock <mutex> lock(mAccessLock);

    mFramesRecord.inUse = false;

    (void)svFramesDesc;
    return {};
}

// Methods from ISurroundView2dSession follow.
Return<void> SurroundView2dSession::get2dMappingInfo(
    get2dMappingInfo_cb _hidl_cb) {
    LOG(DEBUG) << __FUNCTION__;

    _hidl_cb(mInfo);
    return {};
}

Return<SvResult> SurroundView2dSession::set2dConfig(
    const Sv2dConfig& sv2dConfig) {
    LOG(DEBUG) << __FUNCTION__;
    scoped_lock <mutex> lock(mAccessLock);

    if (sv2dConfig.width <=0 || sv2dConfig.width > 4096) {
        LOG(WARNING) << "The width of 2d config is out of the range (0, 4096]"
                     << "Ignored!";
        return SvResult::INVALID_ARG;
    }

    mConfig.width = sv2dConfig.width;
    mConfig.blending = sv2dConfig.blending;
    mHeight = mConfig.width * mInfo.height / mInfo.width;

    if (mStream != nullptr) {
        LOG(DEBUG) << "Notify SvEvent::CONFIG_UPDATED";
        mStream->notify(SvEvent::CONFIG_UPDATED);
    }

    return SvResult::OK;
}

Return<void> SurroundView2dSession::get2dConfig(get2dConfig_cb _hidl_cb) {
    LOG(DEBUG) << __FUNCTION__;

    _hidl_cb(mConfig);
    return {};
}

Return<void> SurroundView2dSession::projectCameraPoints(const hidl_vec<Point2dInt>& points2dCamera,
                                                        const hidl_string& cameraId,
                                                        projectCameraPoints_cb _hidl_cb) {
    LOG(DEBUG) << __FUNCTION__;
    std::vector<Point2dFloat> outPoints;
    bool cameraIdFound = false;
    int cameraIndex = 0;
    // Note: mEvsCameraIds must be in the order front, right, rear, left.
    for (auto& evsCameraId : mEvsCameraIds) {
        if (cameraId == evsCameraId) {
            cameraIdFound = true;
            LOG(DEBUG) << "Camera id found for projection: " << cameraId;
            break;
        }
        cameraIndex++;
    }

    if (!cameraIdFound) {
        LOG(ERROR) << "Camera id not found for projection: " << cameraId;
        _hidl_cb(outPoints);
        return {};
    }

    int width = mConfig.width;
    int height = mHeight;
    for (const auto& cameraPoint : points2dCamera) {
        Point2dFloat outPoint = {false, 0.0, 0.0};
        // Check of the camear point is within the camera resolution bounds.
        if (cameraPoint.x < 0 || cameraPoint.x > width - 1 || cameraPoint.y < 0 ||
            cameraPoint.y > height - 1) {
            LOG(WARNING) << "Camera point (" << cameraPoint.x << ", " << cameraPoint.y
                         << ") is out of camera resolution bounds.";
            outPoint.isValid = false;
            outPoints.push_back(outPoint);
            continue;
        }

        // Project points using mSurroundView function.
        const Coordinate2dInteger camPoint(cameraPoint.x, cameraPoint.y);
        Coordinate2dFloat projPoint2d(0.0, 0.0);

        outPoint.isValid =
                mSurroundView->GetProjectionPointFromRawCameraToSurroundView2d(camPoint,
                                                                               cameraIndex,
                                                                               &projPoint2d);
        outPoint.x = projPoint2d.x;
        outPoint.y = projPoint2d.y;
        outPoints.push_back(outPoint);
    }

    _hidl_cb(outPoints);
    return {};
}

bool SurroundView2dSession::handleFrames(int sequenceId) {
    LOG(INFO) << __FUNCTION__ << "Handling sequenceId " << sequenceId << ".";

    // TODO(b/157498592): Now only one sets of EVS input frames and one SV
    // output frame is supported. Implement buffer queue for both of them.
    {
        scoped_lock<mutex> lock(mAccessLock);

        if (mFramesRecord.inUse) {
            LOG(DEBUG) << "Notify SvEvent::FRAME_DROPPED";
            mStream->notify(SvEvent::FRAME_DROPPED);
            return true;
        }
    }

    if (mOutputWidth != mConfig.width || mOutputHeight != mHeight) {
        LOG(DEBUG) << "Config changed. Re-allocate memory."
                   << " Old width: "
                   << mOutputWidth
                   << " Old height: "
                   << mOutputHeight
                   << " New width: "
                   << mConfig.width
                   << " New height: "
                   << mHeight;
        delete[] static_cast<char*>(mOutputPointer.data_pointer);
        mOutputWidth = mConfig.width;
        mOutputHeight = mHeight;
        mOutputPointer.height = mOutputHeight;
        mOutputPointer.width = mOutputWidth;
        mOutputPointer.format = Format::RGB;
        mOutputPointer.data_pointer =
            new char[mOutputHeight * mOutputWidth * kOutputNumChannels];

        if (!mOutputPointer.data_pointer) {
            LOG(ERROR) << "Memory allocation failed. Exiting.";
            return false;
        }

        android_auto::surround_view::Size2dInteger size =
                      android_auto::surround_view::Size2dInteger(mOutputWidth, mOutputHeight);
        mSurroundView->Update2dOutputResolution(size);

        mSvTexture = new GraphicBuffer(mOutputWidth,
                                       mOutputHeight,
                                       HAL_PIXEL_FORMAT_RGB_888,
                                       1,
                                       GRALLOC_USAGE_HW_TEXTURE |
                                       GRALLOC_USAGE_SW_READ_OFTEN|
                                       GRALLOC_USAGE_SW_WRITE_OFTEN,
                                       "SvTexture");
        if (mSvTexture->initCheck() == OK) {
            LOG(INFO) << "Successfully allocated Graphic Buffer";
        } else {
            LOG(ERROR) << "Failed to allocate Graphic Buffer";
            return false;
        }
    }

    LOG(INFO) << "Output Pointer data format: " << mOutputPointer.format;

#ifdef DEBUG_SURROUNDVIEW_2D
    // dump out the buffer which is the input of Get2dSurroundView
    for (int i=0; i<4; i++) {
            char filename[128];
            memset(filename, 0, 128);
            if (nu%20 ==0) {
                sprintf(filename, "/data/%s-frame_final-%d-%d.rgb",
                       "SV", nu, i);
                dump_frame_to_file((char *)(mInputPointers[i].cpu_data_pointer),
                        mInputPointers[i].width  * mInputPointers[i].height * 3, filename);
            }
    }
#endif

    if (mInputPointers.size() == 4
        && mInputPointers[0].cpu_data_pointer != nullptr) {
        LOG(INFO) << "ReadImages succeeded";
    } else {
        LOG(ERROR) << "Failed to read images";
        return false;
    }


#ifndef ENABLE_IMX_CORELIB
    if (mSurroundView->Get2dSurroundView(mInputPointers, &mOutputPointer)) {
        LOG(INFO) << "Get2dSurroundView succeeded";
    } else {
        LOG(ERROR) << "Get2dSurroundView failed. "
                   << "Using memset to initialize to gray";
        memset(mOutputPointer.data_pointer, kGrayColor,
               mOutputHeight * mOutputWidth * kOutputNumChannels);
    }
#else
    bool ret;
    ret = mImx2DSV->GetSVBuffer(mInputPoint, mOutputPointer.data_pointer, 3);

    if (!ret) {
        memset(mOutputPointer.data_pointer, kGrayColor,
            mOutputHeight * mOutputWidth * kOutputNumChannels);
    }
#endif

    void* textureDataPtr = nullptr;
    mSvTexture->lock(GRALLOC_USAGE_SW_WRITE_OFTEN
                     | GRALLOC_USAGE_SW_READ_OFTEN,
                     &textureDataPtr);
    if (!textureDataPtr) {
        LOG(ERROR) << "Failed to gain write access to GraphicBuffer!";
        return false;
    }

    // Note: there is a chance that the stride of the texture is not the same
    // as the width. For example, when the input frame is 1920 * 1080, the
    // width is 1080, but the stride is 2048. So we'd better copy the data line
    // by line, instead of single memcpy.
    uint8_t* writePtr = static_cast<uint8_t*>(textureDataPtr);
    uint8_t* readPtr = static_cast<uint8_t*>(mOutputPointer.data_pointer);
    const int readStride = mOutputWidth * kOutputNumChannels;
    const int writeStride = mSvTexture->getStride() * kOutputNumChannels;
    if (readStride == writeStride) {
        memcpy(writePtr, readPtr, readStride * mSvTexture->getHeight());
    } else {
        for (int i=0; i<mSvTexture->getHeight(); i++) {
            memcpy(writePtr, readPtr, readStride);
            writePtr = writePtr + writeStride;
            readPtr = readPtr + readStride;
        }
    }

    LOG(INFO) << "memcpy finished";
    mSvTexture->unlock();

    ANativeWindowBuffer* buffer = mSvTexture->getNativeBuffer();
    LOG(DEBUG) << "ANativeWindowBuffer->handle: "
               << buffer->handle;

    {
        scoped_lock<mutex> lock(mAccessLock);

        mFramesRecord.frames.svBuffers.resize(1);
        SvBuffer& svBuffer = mFramesRecord.frames.svBuffers[0];
        svBuffer.viewId = kSv2dViewId;
        svBuffer.hardwareBuffer.nativeHandle = buffer->handle;
        AHardwareBuffer_Desc* pDesc =
            reinterpret_cast<AHardwareBuffer_Desc*>(
                &svBuffer.hardwareBuffer.description);
        pDesc->width = mOutputWidth;
        pDesc->height = mOutputHeight;
        pDesc->layers = 1;
        pDesc->usage = GRALLOC_USAGE_HW_TEXTURE;
        pDesc->stride = mSvTexture->getStride();
        pDesc->format = HAL_PIXEL_FORMAT_RGB_888;
        mFramesRecord.frames.timestampNs = elapsedRealtimeNano();
        mFramesRecord.frames.sequenceId = sequenceId;

        mFramesRecord.inUse = true;
        mStream->receiveFrames(mFramesRecord.frames);
    }

    return true;
}

bool SurroundView2dSession::initialize() {
    lock_guard<mutex> lock(mAccessLock, adopt_lock);

    if (!setupEvs()) {
        LOG(ERROR) << "Failed to setup EVS components for 2d session";
        return false;
    }

    // TODO(b/150412555): ask core-lib team to add API description for "create"
    // method in the .h file.
    // The create method will never return a null pointer based the API
    // description.
    mSurroundView = unique_ptr<SurroundView>(Create());

#ifndef ENABLE_IMX_CORELIB
    SurroundViewStaticDataParams params =
            SurroundViewStaticDataParams(
                    // currently it can only get the logic cameta metadata, it can't set the physical camera
                    // cameta metadata. if use mCameraParams, the four camera data is the same.
                    mCameraParams,
                    mIOModuleConfig->sv2dConfig.sv2dParams,
                    mIOModuleConfig->sv3dConfig.sv3dParams,
                    GetUndistortionScales(),
                    // set the car model as null now.
                    // TODO: need refine this part when regulator the sv parameter
                    GetBoundingBox(),
                    map<string, CarTexture>(),
                    map<string, CarPart>());
    mSurroundView->SetStaticData(params);
    if (mSurroundView->Start2dPipeline()) {
        LOG(INFO) << "Start2dPipeline succeeded";
    } else {
        LOG(ERROR) << "Start2dPipeline failed";
        return false;
    }
#else
    mImx2DSV = new Imx2DSV();
    ImxSV2DParams sv2DParams = ImxSV2DParams(imx::Size2dInteger(mCameraParams[0].size.width,
                 mCameraParams[0].size.height),
                 imx::Size2dInteger(mIOModuleConfig->sv2dConfig.sv2dParams.resolution.width,
                    mIOModuleConfig->sv2dConfig.sv2dParams.resolution.height),
                 imx::Size2dFloat(mIOModuleConfig->sv2dConfig.sv2dParams.physical_size.width,
                    mIOModuleConfig->sv2dConfig.sv2dParams.physical_size.height));

    mImx2DSV->SetConfigs(sv2DParams, mImxCameraParams.mEvsRotations,
                mImxCameraParams.mEvsTransforms, mImxCameraParams.mKs, mImxCameraParams.mDs);
    mImx2DSV->startSV();

#endif
    mInputPointers.resize(kNumFrames);
    for (int i = 0; i < kNumFrames; i++) {
        mInputPointers[i].width = mCameraParams[i].size.width;
        mInputPointers[i].height = mCameraParams[i].size.height;
        mInputPointers[i].format = Format::RGB;
        mInputPointers[i].cpu_data_pointer =
                (void*)new uint8_t[mInputPointers[i].width *
                                   mInputPointers[i].height *
                                   kInputNumChannels];
#ifdef ENABLE_IMX_CORELIB
        // NOTE: do not make mInputPoint as local variable, because it will release cpu_data_pointer
        // once this function is done.
        // it cause sv camera do not work from the second frame.
        shared_ptr<char> p((char *)mInputPointers[i].cpu_data_pointer);
        mInputPoint.push_back(p);
#endif
    }
    LOG(INFO) << "Allocated " << kNumFrames << " input pointers";

    mOutputWidth = mIOModuleConfig->sv2dConfig.sv2dParams.resolution.width;
    mOutputHeight = mIOModuleConfig->sv2dConfig.sv2dParams.resolution.height;

    mConfig.width = mOutputWidth;
    mConfig.blending = SvQuality::HIGH;
    mHeight = mOutputHeight;

    mOutputPointer.height = mOutputHeight;
    mOutputPointer.width = mOutputWidth;
    mOutputPointer.format = Format::RGB;
    mOutputPointer.data_pointer = new char[
        mOutputHeight * mOutputWidth * kOutputNumChannels];

    if (!mOutputPointer.data_pointer) {
        LOG(ERROR) << "Memory allocation failed. Exiting.";
        return false;
    }

    mSvTexture = new GraphicBuffer(mOutputWidth,
                                   mOutputHeight,
                                   HAL_PIXEL_FORMAT_RGB_888,
                                   1,
                                   GRALLOC_USAGE_HW_TEXTURE |
                                   GRALLOC_USAGE_SW_READ_OFTEN|
                                   GRALLOC_USAGE_SW_WRITE_OFTEN,
                                   "SvTexture");

    // Note: sv2dParams is in meters while mInfo must be in milli-meters.
    mInfo.width = mIOModuleConfig->sv2dConfig.sv2dParams.physical_size.width * 1000.0;
    mInfo.height = mIOModuleConfig->sv2dConfig.sv2dParams.physical_size.height * 1000.0;
    mInfo.center.isValid = true;
    mInfo.center.x = mIOModuleConfig->sv2dConfig.sv2dParams.physical_center.x * 1000.0;
    mInfo.center.y = mIOModuleConfig->sv2dConfig.sv2dParams.physical_center.y * 1000.0;

    if (mSvTexture->initCheck() == OK) {
        LOG(INFO) << "Successfully allocated Graphic Buffer";
    } else {
        LOG(ERROR) << "Failed to allocate Graphic Buffer";
        return false;
    }

    mIsInitialized = true;
    return true;
}

bool SurroundView2dSession::setupEvs() {
    // Reads the camera related information from the config object
    const string evsGroupId = mIOModuleConfig->cameraConfig.evsGroupId;

    // Setup for EVS
    LOG(INFO) << "Requesting camera list";
    mEvs->getCameraList_1_1(
            [this, evsGroupId] (hidl_vec<CameraDesc> cameraList) {
        LOG(INFO) << "Camera list callback received " << cameraList.size();
        for (auto&& cam : cameraList) {
            LOG(INFO) << "Found camera " << cam.v1.cameraId;
            if (cam.v1.cameraId == evsGroupId) {
                mCameraDesc = cam;
            }
        }
    });

    bool foundCfg = false;
    std::unique_ptr<Stream> targetCfg(new Stream());

    // This logic picks the configuration with the largest area that supports
    // RGBA8888 format
    int32_t maxArea = 0;
    camera_metadata_entry_t streamCfgs;
    if (!find_camera_metadata_entry(
             reinterpret_cast<camera_metadata_t *>(mCameraDesc.metadata.data()),
             ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
             &streamCfgs)) {
        // Stream configurations are found in metadata
        RawStreamConfig *ptr = reinterpret_cast<RawStreamConfig *>(
            streamCfgs.data.i32);
        for (unsigned idx = 0; idx < streamCfgs.count; idx += kStreamCfgSz) {
            if (ptr->direction ==
                ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
                ptr->format == HAL_PIXEL_FORMAT_RGB_888) {
                if (ptr->width * ptr->height > maxArea) {
                    targetCfg->id = ptr->id;
                    targetCfg->width = ptr->width;
                    targetCfg->height = ptr->height;

                    // This client always wants below input data format
                    targetCfg->format =
                        static_cast<GraphicsPixelFormat>(
                            HAL_PIXEL_FORMAT_RGB_888);

                    maxArea = ptr->width * ptr->height;

                    foundCfg = true;
                }
            }
            ++ptr;
        }
    } else {
        LOG(WARNING) << "No stream configuration data is found; "
                     << "default parameters will be used.";
    }

    if (!foundCfg) {
        LOG(INFO) << "No config was found";
        targetCfg = nullptr;
        return false;
    }

    string camId = mCameraDesc.v1.cameraId.c_str();
    mCamera = mEvs->openCamera_1_1(camId.c_str(), *targetCfg);
    if (mCamera == nullptr) {
        LOG(ERROR) << "Failed to allocate EVS Camera interface for " << camId;
        return false;
    } else {
        LOG(INFO) << "Camera " << camId << " is opened successfully";
    }

    map<string, AndroidCameraParams> cameraIdToAndroidParameters;
    for (const auto& id : mIOModuleConfig->cameraConfig.evsCameraIds) {
        AndroidCameraParams params;
        if (getAndroidCameraParams(mCamera, id, params)) {
            cameraIdToAndroidParameters.emplace(id, params);
            LOG(INFO) << "Camera parameters are fetched successfully for "
                      << "physical camera: " << id;
        } else {
            LOG(ERROR) << "Failed to get camera parameters for "
                       << "physical camera: " << id;
            return false;
        }
    }

    mCameraParams =
            convertToSurroundViewCameraParams(cameraIdToAndroidParameters, mIOModuleConfig);

    for (auto& camera : mCameraParams) {
        camera.size.width = targetCfg->width;
        camera.size.height = targetCfg->height;

        camera.circular_fov = 120;
    }

#ifdef ENABLE_IMX_CORELIB
    mImxCameraParams =
         convertToImxSurroundViewCameraParams(cameraIdToAndroidParameters, mIOModuleConfig);
#endif

    return true;
}

bool SurroundView2dSession::startEvs() {
    mFramesHandler = new FramesHandler(mCamera, this);
    Return<EvsResult> result = mCamera->startVideoStream(mFramesHandler);
    if (result != EvsResult::OK) {
        LOG(ERROR) << "Failed to start video stream";
        return false;
    } else {
        LOG(INFO) << "Video stream was started successfully";
    }

    return true;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace sv
}  // namespace automotive
}  // namespace hardware
}  // namespace android
