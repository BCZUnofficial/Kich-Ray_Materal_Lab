#include "AttachmentHistory.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool same(const lab::AttachmentTransform& a,const lab::AttachmentTransform& b){
    return a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&
        a.rotation.x==b.rotation.x&&a.rotation.y==b.rotation.y&&a.rotation.z==b.rotation.z&&a.scale==b.scale;
}
lab::RecentAttachment item(const std::string& bone,const std::string& path){
    lab::RecentAttachment entry;entry.bone=bone;entry.path=path;return entry;
}

void restartPreservesUnicodeAndFitting(){
    lab::AttachmentHistory history;
    auto weapon=item("bip01_r_hand",u8R"(C:\Мой мод\gamedata\meshes\оружие\автомат.ogf)");
    weapon.transform.position={.013579f,-.254123f,1.2345678f};
    weapon.transform.rotation={-90.f,179.125f,.03125f};weapon.transform.scale=.873421f;
    const auto food=item("bip01_l_hand",u8R"(D:\Проверка моделей\еда\хлеб.ogf)");
    require(history.Remember(food)&&history.Remember(weapon),"remember Unicode prop paths and local fitting");
    const auto serialized=history.Serialize();
    lab::AttachmentHistory afterRestart;std::string error="old error";
    require(afterRestart.Deserialize(serialized,error)&&error.empty(),"history must reopen in a new process state");
    const auto right=afterRestart.ForBone("BIP01_R_HAND"),left=afterRestart.ForBone("bip01_l_hand");
    require(right.size()==1&&left.size()==1,"each bone keeps its own recent prop list across restart");
    require(right[0].path==weapon.path&&left[0].path==food.path,"UTF-8 paths with spaces and backslashes round-trip byte-for-byte");
    require(same(right[0].transform,weapon.transform),"all local fitting parameters round-trip at float precision");
    require(afterRestart.Serialize()==serialized,"saving reopened history preserves ordering and numbers");
    lab::AttachmentHistory secondRestart;
    require(secondRestart.Deserialize(afterRestart.Serialize(),error)&&secondRestart.ForBone("bip01_r_hand")[0].path==weapon.path,
            "recent props remain available after repeated application restarts");
}

void perBoneMruAndCapacity(){
    lab::AttachmentHistory history;
    const auto left=item("bip01_l_hand","C:/models/bread.ogf");
    require(history.Remember(left),"remember independent left-hand prop");
    for(int i=0;i<16;++i)
        require(history.Remember(item("bip01_r_hand","C:/models/weapon_"+std::to_string(i)+".ogf")),"remember right-hand prop series");
    auto recent=history.ForBone("bip01_r_hand");
    require(recent.size()==12&&recent.front().path=="C:/models/weapon_15.ogf"&&recent.back().path=="C:/models/weapon_4.ogf",
            "each bone retains the twelve most recently used props in order");
    auto reused=item("BIP01_R_HAND",R"(c:\MODELS\weapon_8.ogf)");reused.transform.position.y=.42f;reused.transform.scale=1.75f;
    require(history.Remember(reused),"reuse a prop through equivalent Windows path spelling");
    recent=history.ForBone("bip01_r_hand");
    require(recent.size()==12&&recent.front().path==reused.path&&same(recent.front().transform,reused.transform),
            "reusing a prop moves it to the front and updates its fitting without duplicating it");
    require(recent[1].path=="C:/models/weapon_15.ogf"&&recent.back().path=="C:/models/weapon_4.ogf",
            "MRU promotion must not evict another prop when only an existing entry changed");
    require(history.ForBone("bip01_l_hand").size()==1&&history.ForBone("bip01_l_hand")[0].path==left.path,
            "using right-hand props cannot discard left-hand history");
    std::string error;lab::AttachmentHistory reopened;
    require(reopened.Deserialize(history.Serialize(),error)&&reopened.Serialize()==history.Serialize(),
            "per-bone MRU order survives serialization");

    lab::AttachmentHistory manyBones;
    for(int i=0;i<260;++i)
        require(manyBones.Remember(item("bip_socket_"+std::to_string(i),"C:/models/prop.ogf")),"remember bounded multi-skeleton history");
    require(manyBones.ForBone("bip_socket_0").empty()&&manyBones.ForBone("bip_socket_3").empty()&&
            manyBones.ForBone("bip_socket_4").size()==1&&manyBones.ForBone("bip_socket_259").size()==1,
            "global capacity evicts oldest entries while retaining recent skeleton slots");
}

void corruptHistoryCannotReplaceWorkingHistory(){
    lab::AttachmentHistory history;std::string error;
    const auto kept=item("bip01_r_hand",u8R"(C:\Предметы\водка.ogf)");
    require(history.Remember(kept),"seed working recent-prop history");const auto original=history.Serialize();
    const auto reject=[&](const std::string& bytes){
        require(!history.Deserialize(bytes,error)&&!error.empty(),"malformed history must report an error");
        require(history.Serialize()==original,"failed history load must preserve all working recent props");
    };
    reject("");reject("MaterialLabAttachments 2\n");
    reject(original+"\"bip01_l_hand\" \"C:/models/bread.ogf\" 0 0 0 0 0\n");
    reject(original+"\"bip01_l_hand\" \"C:/models/bread.ogf\" 0 0 0 0 0 0 0\n");
    reject(original+"\"bip01_l_hand\" \"C:/models/bread.ogf\" 0 0 0 0 0 0 -1\n");
    reject(original+"\"bip01_l_hand\" \"C:/models/bread.ogf\" 0 0 0 0 0 0 nan\n");
    reject(original+"\"bip01_l_hand\" \"C:/models/bread.ogf\" 0 0 0 0 0 0 1 trailing\n");
    reject(original+"\"bip01_l_hand\" \""+std::string(32769,'x')+"\" 0 0 0 0 0 0 1\n");
    std::string nul=original;nul.insert(nul.find("bip01"),1,'\0');reject(nul);
    std::string tooMany="MaterialLabAttachments 1\n";
    for(int i=0;i<257;++i)tooMany+="\"bip"+std::to_string(i)+"\" \"prop.ogf\" 0 0 0 0 0 0 1\n";
    reject(tooMany);
    auto invalid=kept;invalid.transform.scale=std::numeric_limits<float>::infinity();
    require(!history.Remember(invalid)&&history.Serialize()==original,"invalid fitting cannot overwrite a remembered prop");
    invalid=kept;invalid.bone+="\nother";
    require(!history.Remember(invalid)&&history.Serialize()==original,"bone line breaks cannot corrupt persisted rows");
    invalid=kept;invalid.path+="\rbroken";
    require(!history.Remember(invalid)&&history.Serialize()==original,"path line breaks cannot corrupt persisted rows");
    require(history.Deserialize("MaterialLabAttachments 1\n",error)&&error.empty()&&history.ForBone(kept.bone).empty(),
            "an explicitly valid empty history replaces the previous history");
}

void oneMiBBudget(){
    lab::AttachmentHistory history;
    // Long paths stress the serialized-byte bound before the entry-count bound.
    for(int i=0;i<40;++i){
        auto entry=item("bip_socket_"+std::to_string(i),"C:/"+std::string(32750,'x')+".ogf");
        require(history.Remember(entry),"valid long paths can be remembered");
        require(history.Serialize().size()<=lab::AttachmentHistory::MaxBytes,"history must never write beyond one MiB");
    }
    require(history.ForBone("bip_socket_0").empty()&&history.ForBone("bip_socket_39").size()==1,
            "byte-budget eviction retains the newest prop");
    std::string error;const auto preserved=history.Serialize();lab::AttachmentHistory reopened;
    require(reopened.Deserialize(preserved,error)&&reopened.Serialize()==preserved,"large bounded history remains readable after restart");
    require(!history.Deserialize(std::string(lab::AttachmentHistory::MaxBytes+1,'x'),error)&&history.Serialize()==preserved,
            "oversized history is rejected transactionally before parsing");
    std::string exact="MaterialLabAttachments 1\n";
    exact.resize(lab::AttachmentHistory::MaxBytes,'\n');
    require(reopened.Deserialize(exact,error)&&reopened.ForBone("bip_socket_39").empty(),
            "the exact one MiB input boundary remains valid");
}
}

int main(){
    try{
        restartPreservesUnicodeAndFitting();perBoneMruAndCapacity();corruptHistoryCannotReplaceWorkingHistory();oneMiBBudget();
        std::cout<<"PASS: attachment history restart, UTF-8 paths, per-bone MRU, fitting, transactional corruption handling and one MiB bound\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
