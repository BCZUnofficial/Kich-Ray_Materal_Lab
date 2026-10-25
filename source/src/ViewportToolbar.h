#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>

namespace lab {
struct ViewTool {int id;const wchar_t* label;const wchar_t* tip;bool toggle=false;};

// Native common controls retain keyboard access, screen-reader names and tooltips.
// Only their paint is customized; no UI runtime or package download is required.
class ViewportToolbar {
    HWND bar_=nullptr,hint_=nullptr;
    const ViewTool* tools_=nullptr;
    size_t count_=0;
    int dpi_=96;
    int px(int value) const{return MulDiv(value,dpi_,96);}
public:
    static constexpr int Height=84;
    HWND window() const{return bar_;}
    HWND hint() const{return hint_;}
    bool create(HWND parent,HFONT font,const ViewTool* tools,size_t count,int dpi=96) {
        tools_=tools;count_=count;dpi_=dpi;
        bar_=CreateWindowExW(0,TOOLBARCLASSNAMEW,L"Режимы просмотра",WS_CHILD|WS_VISIBLE|WS_TABSTOP|
            TBSTYLE_FLAT|TBSTYLE_TOOLTIPS|TBSTYLE_LIST|CCS_NOPARENTALIGN|CCS_NORESIZE|CCS_NODIVIDER,
            0,0,600,42,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!bar_)return false;
        SendMessageW(bar_,TB_BUTTONSTRUCTSIZE,sizeof(TBBUTTON),0);
        SendMessageW(bar_,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SendMessageW(bar_,TB_SETBITMAPSIZE,0,MAKELPARAM(0,0));
        SendMessageW(bar_,TB_SETBUTTONSIZE,0,MAKELPARAM(px(42),px(40)));
        SendMessageW(bar_,TB_SETMAXTEXTROWS,1,0);
        for(size_t i=0;i<count;++i){
            if(tools[i].id==0){TBBUTTON sep{};sep.fsStyle=BTNS_SEP;sep.iBitmap=px(10);
                SendMessageW(bar_,TB_ADDBUTTONS,1,reinterpret_cast<LPARAM>(&sep));continue;}
            TBBUTTON b{};b.iBitmap=I_IMAGENONE;b.idCommand=tools[i].id;
            b.fsState=TBSTATE_ENABLED;b.fsStyle=BTNS_SHOWTEXT|(tools[i].toggle?BTNS_CHECK:BTNS_BUTTON);
            b.iString=reinterpret_cast<INT_PTR>(tools[i].label);
            SendMessageW(bar_,TB_ADDBUTTONS,1,reinterpret_cast<LPARAM>(&b));
        }
        HWND tips=reinterpret_cast<HWND>(SendMessageW(bar_,TB_GETTOOLTIPS,0,0));
        if(tips){SendMessageW(tips,TTM_SETMAXTIPWIDTH,0,420);
            SendMessageW(tips,TTM_SETDELAYTIME,TTDT_INITIAL,650);
            SendMessageW(tips,TTM_SETDELAYTIME,TTDT_RESHOW,120);
            SendMessageW(tips,TTM_SETDELAYTIME,TTDT_AUTOPOP,18000);}
        hint_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX|SS_ENDELLIPSIS,
            0,0,600,24,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(hint_,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        return hint_!=nullptr;
    }
    void layout(int x,int y,int width) {
        MoveWindow(bar_,x+px(12),y+px(6),std::max(1,width-px(24)),px(42),TRUE);
        MoveWindow(hint_,x+px(16),y+px(54),std::max(1,width-px(32)),px(24),TRUE);
    }
    void text(const wchar_t* text) {SetWindowTextW(hint_,text);}
    void check(int id,bool checked) {SendMessageW(bar_,TB_CHECKBUTTON,id,MAKELONG(checked,0));}
    void enable(int id,bool enabled) {SendMessageW(bar_,TB_ENABLEBUTTON,id,MAKELONG(enabled,0));}
    bool notify(NMHDR* h,LRESULT& result) {
        if(!h||h->hwndFrom!=bar_)return false;
        if(h->code==TBN_GETINFOTIPW){auto* tip=reinterpret_cast<NMTBGETINFOTIPW*>(h);
            for(size_t i=0;i<count_;++i)if(tools_[i].id==tip->iItem){
                lstrcpynW(tip->pszText,tools_[i].tip,tip->cchTextMax);break;}
            result=0;return true;
        }
        if(h->code!=NM_CUSTOMDRAW)return false;
        auto* cd=reinterpret_cast<NMTBCUSTOMDRAW*>(h);
        auto fill=[&](RECT rect,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(cd->nmcd.hdc,&rect,b);DeleteObject(b);};
        if(cd->nmcd.dwDrawStage==CDDS_PREERASE){fill(cd->nmcd.rc,RGB(29,35,46));result=CDRF_SKIPDEFAULT;return true;}
        if(cd->nmcd.dwDrawStage==CDDS_PREPAINT){result=CDRF_NOTIFYITEMDRAW;return true;}
        if(cd->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){
            const int id=int(cd->nmcd.dwItemSpec);
            const bool active=(SendMessageW(bar_,TB_GETSTATE,id,0)&TBSTATE_CHECKED)!=0;
            const bool disabled=(cd->nmcd.uItemState&CDIS_DISABLED)!=0;
            const bool hot=(cd->nmcd.uItemState&(CDIS_HOT|CDIS_SELECTED))!=0;
            RECT r=cd->nmcd.rc;InflateRect(&r,-2,-2);
            fill(cd->nmcd.rc,RGB(29,35,46));
            const COLORREF bg=active?RGB(47,113,222):(hot?RGB(57,69,88):RGB(40,49,63));
            HBRUSH brush=CreateSolidBrush(bg);HGDIOBJ oldBrush=SelectObject(cd->nmcd.hdc,brush);
            HGDIOBJ oldPen=SelectObject(cd->nmcd.hdc,GetStockObject(NULL_PEN));
            RoundRect(cd->nmcd.hdc,r.left,r.top,r.right,r.bottom,8,8);
            SelectObject(cd->nmcd.hdc,oldPen);SelectObject(cd->nmcd.hdc,oldBrush);DeleteObject(brush);
            SetBkMode(cd->nmcd.hdc,TRANSPARENT);
            SetTextColor(cd->nmcd.hdc,disabled?RGB(108,120,138):RGB(238,243,251));
            for(size_t i=0;i<count_;++i)if(tools_[i].id==id){DrawTextW(cd->nmcd.hdc,tools_[i].label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);break;}
            if(cd->nmcd.uItemState&CDIS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(cd->nmcd.hdc,&r);}
            result=CDRF_SKIPDEFAULT;return true;
        }
        result=CDRF_DODEFAULT;return true;
    }
};
}
