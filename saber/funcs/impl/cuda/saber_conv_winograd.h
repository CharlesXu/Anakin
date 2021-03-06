/* Copyright (c) 2018 Anakin Authors, Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef ANAKIN_SABER_FUNCS_IMPL_CUDA_SABER_WINOGRAD_CONV_H
#define ANAKIN_SABER_FUNCS_IMPL_CUDA_SABER_WINOGRAD_CONV_H

#include <vector>
#include "saber/funcs/impl/impl_conv.h"
#include "sass_funcs.h"
#include "saber/funcs/impl/cuda/saber_activation.h"
#include "saber/funcs/funcs_utils.h"

namespace anakin{

namespace saber{

template <DataType OpDtype>
class SaberWinogradConv : public ImplBase<
        NV, OpDtype, ConvParam<NV> > {
public:
    typedef typename DataTrait<NV, OpDtype>::Dtype OpDataType;
    typedef ImplBase<NV, OpDtype, ConvParam<NV> > Impl_t;
    SaberWinogradConv() = default;
    ~SaberWinogradConv() {
        delete _saber_act;
    }

    virtual SaberStatus init(const std::vector<Tensor<NV> *>& inputs,
                             std::vector<Tensor<NV> *>& outputs,
                             ConvParam<NV>& param, Context<NV> &ctx)
    {
        this->_ctx = &ctx;
        _use_saber_act = param.activation_param.has_active
            && !(param.activation_param.active == Active_relu
            && fabsf(param.activation_param.negative_slope) < 1e-6f);
        _use_saber_act = _use_saber_act ||
            (param.bias()->valid_size() == 0 && param.activation_param.has_active);
        if (param.activation_param.has_active) {
            if (_use_saber_act) {
                _saber_act = new SaberActivation<NV, AK_FLOAT>;
                _saber_act->init(inputs, outputs, param.activation_param, ctx);
            }
        }
        return create(inputs, outputs, param, ctx);
    }

    virtual SaberStatus create(const std::vector<Tensor<NV> *>& inputs,
                               std::vector<Tensor<NV> *>& outputs,
                               ConvParam<NV>& param, Context<NV>& ctx)
    {
        if (_saber_act != nullptr)
            _saber_act->create(outputs, outputs, param.activation_param, ctx);
    }

    virtual SaberStatus dispatch(const std::vector<Tensor<NV>*>& inputs,
                                 std::vector<Tensor<NV>*>& outputs,
                                 ConvParam<NV>& param);

private:
    SaberActivation<NV, OpDtype> *_saber_act{nullptr};
    bool _use_saber_act{false};

};
}

}


#endif //ANAKIN_SABER_FUNCS_IMPL_CUDA_SABER_WINOGRAD_CONV_H
