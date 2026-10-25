#pragma once
#include <algorithm>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

namespace lab {
inline std::filesystem::path PreferredExportDirectory(const std::filesystem::path& modelTexture,
    const std::filesystem::path& lastExport,const std::filesystem::path& model) {
    if(!modelTexture.empty()&&!modelTexture.parent_path().empty())return modelTexture.parent_path();
    if(!lastExport.empty())return lastExport;
    return model.parent_path();
}
// OGF motion names are usually ASCII. Also fold UTF-8 Cyrillic so case does not
// split Russian names into separate upper/lower-case groups.
inline std::vector<unsigned> AnimationSortKey(const std::string& name){
    std::vector<unsigned> result;
    for(size_t i=0;i<name.size();){unsigned c=static_cast<unsigned char>(name[i++]);
        if(c>=0xc0&&c<0xf8){int remaining=c<0xe0?1:c<0xf0?2:3;c&=(1u<<(6-remaining))-1;
            while(remaining--&&i<name.size())c=(c<<6)|(static_cast<unsigned char>(name[i++])&63);}
        if(c>='A'&&c<='Z')c+='a'-'A';if(c>=0x410&&c<=0x42f)c+=32;if(c==0x401)c=0x451;
        result.push_back(c==0x451?0x435:c); // е/ё share an alphabetic group
    }return result;
}
inline std::vector<size_t> SortedAnimationIndices(const std::vector<std::string>& names){
    std::vector<size_t> order(names.size());std::iota(order.begin(),order.end(),0);
    std::vector<std::vector<unsigned>> keys;for(const auto& name:names)keys.push_back(AnimationSortKey(name));
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return keys[a]<keys[b];});return order;
}
}
