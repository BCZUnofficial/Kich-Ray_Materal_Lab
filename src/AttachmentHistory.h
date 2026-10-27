#pragma once
#include "AttachmentPreview.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace lab {
struct RecentAttachment {
    std::string bone,path; // UTF-8, independent of the Windows ANSI code page.
    AttachmentTransform transform;
};
inline bool IsBipedBone(const std::string& name) {
    std::string lower=name;
    for(char& c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return lower.rfind("bip",0)==0;
}
class AttachmentHistory {
    static std::string key(std::string s) {
        for(char& c:s){if(c=='\\')c='/';if(c>='A'&&c<='Z')c=char(c-'A'+'a');}return s;
    }
    static bool valid(const RecentAttachment& item) {
        return !item.bone.empty()&&item.bone.size()<=256&&!item.path.empty()&&item.path.size()<=32768&&
            item.bone.find_first_of("\r\n\0",0,3)==std::string::npos&&
            item.path.find_first_of("\r\n\0",0,3)==std::string::npos&&ValidAttachmentTransform(item.transform);
    }
    std::vector<RecentAttachment> entries_;
public:
    static constexpr size_t MaxPerBone=12,MaxEntries=256,MaxBytes=1024*1024;
    std::vector<RecentAttachment> ForBone(const std::string& bone)const {
        std::vector<RecentAttachment> result;const auto wanted=key(bone);
        for(const auto& entry:entries_)if(key(entry.bone)==wanted)result.push_back(entry);
        return result;
    }
    bool Remember(RecentAttachment entry) {
        if(!valid(entry))return false;
        const auto bone=key(entry.bone),path=key(entry.path);
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const RecentAttachment& old){return key(old.bone)==bone&&key(old.path)==path;}),entries_.end());
        entries_.insert(entries_.begin(),std::move(entry));
        size_t count=0;
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const RecentAttachment& old){return key(old.bone)==bone&&++count>MaxPerBone;}),entries_.end());
        if(entries_.size()>MaxEntries)entries_.resize(MaxEntries);
        while(!entries_.empty()&&Serialize().size()>MaxBytes)entries_.pop_back();
        return true;
    }
    std::string Serialize()const {
        std::ostringstream out;out.imbue(std::locale::classic());
        out<<"MaterialLabAttachments 1\n"<<std::setprecision(9);
        for(const auto& e:entries_){const auto& t=e.transform;
            out<<std::quoted(e.bone)<<' '<<std::quoted(e.path)<<' '<<t.position.x<<' '<<t.position.y<<' '<<t.position.z<<' '
               <<t.rotation.x<<' '<<t.rotation.y<<' '<<t.rotation.z<<' '<<t.scale<<'\n';}
        return out.str();
    }
    bool Deserialize(const std::string& text,std::string& error) {
        error.clear();if(text.size()>MaxBytes){error="История вложений слишком велика.";return false;}
        std::istringstream input(text);input.imbue(std::locale::classic());std::string line;
        if(!std::getline(input,line)||line!="MaterialLabAttachments 1"){error="Неизвестный формат истории вложений.";return false;}
        std::vector<RecentAttachment> parsed;
        while(std::getline(input,line)){
            if(line.empty())continue;
            std::istringstream row(line);row.imbue(std::locale::classic());RecentAttachment e;auto& t=e.transform;
            if(!(row>>std::quoted(e.bone)>>std::quoted(e.path)>>t.position.x>>t.position.y>>t.position.z>>t.rotation.x>>t.rotation.y>>t.rotation.z>>t.scale)||
                !valid(e)||parsed.size()>=MaxEntries){error="Повреждена запись истории вложений.";return false;}
            row>>std::ws;if(!row.eof()){error="Лишние данные в истории вложений.";return false;}
            parsed.push_back(std::move(e));
        }
        AttachmentHistory next;
        for(auto i=parsed.rbegin();i!=parsed.rend();++i)next.Remember(*i);
        entries_=std::move(next.entries_);return true;
    }
};
}
