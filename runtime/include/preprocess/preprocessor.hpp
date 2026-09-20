#pragma once

#include "common/frame.hpp"
#include "common/model_input.hpp"

// 모든 전처리 구현이 따라야 하는 공통 인터페이스.
class Preprocessor
{
public:
    virtual ~Preprocessor() = default;

    virtual ModelInput preprocess(const Frame& frame) = 0;
};