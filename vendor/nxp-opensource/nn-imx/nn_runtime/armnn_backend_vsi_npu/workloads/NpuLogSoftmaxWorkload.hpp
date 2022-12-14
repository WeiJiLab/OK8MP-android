/****************************************************************************
*
*    Copyright (c) 2020 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/

#pragma once

#include <backendsCommon/CpuTensorHandle.hpp>
#include <backendsCommon/Workload.hpp>
#include <backendsCommon/WorkloadData.hpp>
#include <iostream>
#include "TNpuWorkloads.hpp"

namespace armnn {
template <typename armnn::DataType... DataTypes>
class NpuLogSoftmaxWorkload : public TNpuWorkload<LogSoftmaxQueueDescriptor, DataTypes...> {
   public:
    using base_type = TNpuWorkload<LogSoftmaxQueueDescriptor, DataTypes...>;
    explicit NpuLogSoftmaxWorkload(const LogSoftmaxQueueDescriptor& descriptor, const WorkloadInfo& info)
        : TNpuWorkload<LogSoftmaxQueueDescriptor, DataTypes...>(descriptor, info),
          m_Beta(descriptor.m_Parameters.m_Beta),
          m_Axis(descriptor.m_Parameters.m_Axis) {
        // Add inputs operand
        std::vector<uint32_t> inOperandIds;
        // ONLY 1 input
        NpuTensorHandler* inputTensorHandle =
            dynamic_cast<NpuTensorHandler*>(descriptor.m_Inputs[0]);
        if (inputTensorHandle) {
            uint32_t inputOperandId = this->AddOperandAndSetValue(
                inputTensorHandle->GetTensorInfo(), inputTensorHandle->GetShape(), nullptr);
            inOperandIds.push_back(inputOperandId);
        }

        // Add beta operand
        inOperandIds.push_back(this->AddOperandAndSetValue(m_Beta));

        // Add axis operand
        inOperandIds.push_back(this->AddOperandAndSetValue(m_Axis));

        std::vector<uint32_t> outOperandIds;
        NpuTensorHandler* outputTensorHandle =
            dynamic_cast<NpuTensorHandler*>(descriptor.m_Outputs[0]);
        if (outputTensorHandle) {
            uint32_t outputTensorId = this->AddOperandAndSetValue(
                outputTensorHandle->GetTensorInfo(), outputTensorHandle->GetShape(), nullptr);
            outOperandIds.push_back(outputTensorId);
        }

        this->AddOperation(nnrt::OperationType::LOG_SOFTMAX,
                           inOperandIds.size(),
                           inOperandIds.data(),
                           outOperandIds.size(),
                           outOperandIds.data());
    }

   private:
    float m_Beta;
    int m_Axis;
};
using NpuLogSoftmaxFloat32Workload = NpuLogSoftmaxWorkload<armnn::DataType::Float32>;
using NpuLogSoftmaxFloat16Workload = NpuLogSoftmaxWorkload<armnn::DataType::Float16>;
using NpuLogSoftmaxUint8Workload = NpuLogSoftmaxWorkload<armnn::DataType::QAsymmU8>;
}  // namespace armnn
