#pragma once

#include <vector>

class IReidPostprocessor
{
public:
    virtual ~IReidPostprocessor() = default;
    virtual std::vector<float> process(std::vector<float> embedding) const = 0;
};
