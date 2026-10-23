#pragma once
// 1.3.0: zlib streams for the member patch engines (third_party\zlib-1.3.1, the same zlib the mod build tools use, so the
// same level gives the same bytes). Only whole-buffer calls; every failure, size mismatch or trailing byte is refused.
#include "files.h"
#define ZLIB_CONST
#include "zlib.h"
namespace ssasr {
// Unpacks one complete zlib stream that must decode to exactly `expected` bytes.
inline std::string zlib_unpack(const unsigned char* data,size_t size,size_t expected){
    require(size<=UINT32_MAX&&expected<=256u*1024*1024,"Packed section too large");
    std::string out(expected,'\0');z_stream z{};
    require(inflateInit(&z)==Z_OK,"zlib start failed");
    z.next_in=data;z.avail_in=(uInt)size;z.next_out=(Bytef*)out.data();z.avail_out=(uInt)expected;
    int r=inflate(&z,Z_FINISH);bool whole=r==Z_STREAM_END&&z.avail_out==0&&z.avail_in==0;inflateEnd(&z);
    require(whole,"Packed section is damaged or has another size");return out;
}
// Packs bytes as one zlib stream (window 15, memory level 8, default strategy: the settings of Python's zlib.compress).
inline std::string zlib_pack(const std::string& data,int level){
    require(level>=1&&level<=9&&data.size()<=256u*1024*1024,"Invalid zlib packing request");
    z_stream z{};require(deflateInit(&z,level)==Z_OK,"zlib start failed");
    std::string out(deflateBound(&z,(uLong)data.size()),'\0');
    z.next_in=(const Bytef*)data.data();z.avail_in=(uInt)data.size();z.next_out=(Bytef*)out.data();z.avail_out=(uInt)out.size();
    int r=deflate(&z,Z_FINISH);size_t n=out.size()-z.avail_out;deflateEnd(&z);
    require(r==Z_STREAM_END,"zlib packing failed");out.resize(n);return out;
}
}
