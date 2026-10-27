#pragma once
#include <array>
#include <cstddef>

namespace lab {
// Keyboard ownership is separate from the camera math. A consumed shortcut or
// focus loss must not turn a still-held S into backwards flight on the next frame.
class FlightInput {
public:
    enum Key : size_t { Left, Right, Down, Up, Forward, Back, Count };
    using Keys=std::array<bool,Count>;
    struct Movement { float right=0,up=0,forward=0; };
    void Suspend(){blocked_.fill(true);}
    Movement Sample(const Keys& keys,bool allowed,bool saveChord) {
        for(size_t i=0;i<Count;++i)if(!keys[i])blocked_[i]=false;
        if(!allowed){for(size_t i=0;i<Count;++i)if(keys[i])blocked_[i]=true;return {};}
        if(saveChord&&keys[Back])blocked_[Back]=true;
        auto down=[&](Key key){return keys[key]&&!blocked_[key]?1.f:0.f;};
        return {down(Right)-down(Left),down(Up)-down(Down),down(Forward)-down(Back)};
    }
private:
    Keys blocked_{};
};
}
