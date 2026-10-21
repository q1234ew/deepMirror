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
