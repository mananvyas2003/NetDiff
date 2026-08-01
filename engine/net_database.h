#pragma once

#include "pin_mapper.h"

struct NetRecord
{
    std::string name;

    std::vector<size_t> pins;
};

class NetDatabase
{
private:

    const PinMapper& mapper;

    std::unordered_map<
        std::string,
        NetRecord
    > nets;

public:

    explicit NetDatabase(
        const PinMapper& m);

    void Build();

    void Print() const;

    const NetRecord*
        FindNet(
            const std::string& name) const;
};