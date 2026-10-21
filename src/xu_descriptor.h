#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
inline bool parseXuDescriptorGuid(const uint8_t *data,size_t size,uint8_t (&guid)[16]) {
    if(!data||size<18||size<size_t(18)+data[17]||data[16]==0)return false;
    bool nonzero=false;for(size_t i=0;i<16;++i)nonzero|=data[i]!=0;
    if(!nonzero)return false;
    std::memcpy(guid,data,16);return true;
}
#include <array>
#include <vector>
#include <algorithm>
inline std::vector<std::array<uint8_t,16>> parseXuPropertySets(const uint8_t *data,size_t size) {
    std::vector<std::array<uint8_t,16>> result;
    if(!data||size%16!=0)return result;
    for(size_t offset=0;offset<size;offset+=16) {
        std::array<uint8_t,16> id{};std::memcpy(id.data(),data+offset,16);
        if(std::any_of(id.begin(),id.end(),[](uint8_t v){return v!=0;})&&std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);
    }
    return result;
}
