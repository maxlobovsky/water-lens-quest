#pragma once
#include <algorithm>
#include <cmath>

struct EffectSettings {
    float strength=.35f,speed=.6f,density=1,complexity=.45f,sharpness=0,coverage=1.f;
    bool cycling=true,clear=false;
    bool fullView=false,fullViewAvailable=false;
};
struct UiRect {
    float x,y,w,h;
    bool contains(float px,float py,float margin=0)const{return px>=x-margin&&px<=x+w+margin&&py>=y-margin&&py<=y+h+margin;}
};
struct PanelInput {
    bool tracked=false,pinch=false,onSurface=false;
    int surface=0;
    float x=0,y=0;
};
struct PanelControls {
    enum { None=-1, Water=0, Auto=1, Calm=2, Ripples=3, Deep=4, Reset=5, Hide=6, Open=7, Choppy=8, Mode=9, RowBase=10, RowCount=6, RowEnd=RowBase+RowCount*3 };
    enum Event { Changed=1,CycleChanged=2,Recenter=4 };
    static constexpr UiRect background{-.49f,-.67f,.98f,1.01f};
    // A compact head-relative tab, ~8 x 3 degrees, away from the center of view.
    static constexpr UiRect launcher{.40f,-.48f,.16f,.06f};
    static constexpr float launcherDepth=1.f;
    static constexpr float trackLeft=-.10f,trackWidth=.36f;
    bool open=false;
    int owner=-1;
    struct Hand {
        bool armed=false,down=false;
        int captured=None,hover=None,previous=None;
        double previousAt=-100;
        float previousX=0,previousY=0;
    } hands[2];
    PanelInput pointers[2];
    static float rowY(int row){return -.075f-row*.085f;}
    static UiRect rect(int id){
        if(id==Water)return {-.44f,.12f,.42f,.075f};
        if(id==Auto)return {.02f,.12f,.42f,.075f};
        if((id>=Calm&&id<=Deep)||id==Choppy)return {-.44f+(id==Choppy?3:id-Calm)*.225f,.02f,.205f,.075f};
        if(id==Reset)return {-.44f,-.63f,.20f,.065f};
        if(id==Mode)return {-.20f,-.63f,.64f,.065f};
        if(id==Hide)return {.32f,.255f,.12f,.06f};
        if(id==Open)return launcher;
        if(id>=RowBase&&id<RowEnd){
            const int row=(id-RowBase)/3,part=(id-RowBase)%3;
            const float y=rowY(row)-.0325f;
            if(part==0)return {-.19f,y,.065f,.065f};
            if(part==2)return {.285f,y,.065f,.065f};
            return {trackLeft-.012f,y,trackWidth+.024f,.065f};
        }
        return {0,0,0,0};
    }
    static int hit(const PanelInput& p,bool expanded){
        if(!p.tracked||!p.onSurface)return None;
        if(p.surface==1)return !expanded&&launcher.contains(p.x,p.y)?Open:None;
        if(!expanded)return None;
        for(int id=Water;id<=Hide;id++)if(rect(id).contains(p.x,p.y))return id;
        if(rect(Choppy).contains(p.x,p.y))return Choppy;
        if(rect(Mode).contains(p.x,p.y))return Mode;
        for(int id=RowBase;id<RowEnd;id++)if(rect(id).contains(p.x,p.y))return id;
        return None;
    }
    static float minimum(int row){return row==2?.3f:row==5?1.f:0.f;}
    static float maximum(int row){return row==1?12.f:row==5?3.f:row==2?2.5f:1.f;}
    static float value(const EffectSettings& s,int row){return row==0?s.strength:row==1?s.speed:row==2?s.density:row==3?s.complexity:row==4?s.sharpness:s.coverage;}
    static float fraction(const EffectSettings& s,int row){const float t=(value(s,row)-minimum(row))/(maximum(row)-minimum(row));return row==1?std::sqrt(t):t;}
    static void set(EffectSettings& s,int row,float v){
        if(row==5&&s.fullView)return;
        float* values[]{&s.strength,&s.speed,&s.density,&s.complexity,&s.sharpness,&s.coverage};
        *values[row]=std::clamp(v,minimum(row),maximum(row));if(row!=5)s.cycling=false;
    }
    static bool slider(int id){return id>=RowBase&&id<RowEnd&&(id-RowBase)%3==1;}
    unsigned activate(int id,EffectSettings& s){
        if(id==Open){open=true;return Recenter;}
        if(id==Hide){open=false;return Changed;}
        if(id==Water){s.clear=!s.clear;return Changed;}
        if(id==Auto){s.cycling=!s.cycling;return Changed|CycleChanged;}
        if(id==Mode&&s.fullViewAvailable){s.fullView=!s.fullView;s.coverage=1;return Changed;}
        if(id==Reset){const bool mode=s.fullView,available=s.fullViewAvailable;s={};s.fullView=mode;s.fullViewAvailable=available;s.cycling=false;return Changed;}
        if((id>=Calm&&id<=Deep)||id==Choppy){
            constexpr float presets[4][5]{{.2f,.3f,.55f,.15f,0},{.45f,.65f,2.1f,.55f,.2f},{.85f,.15f,.85f,.9f,.15f},{.7f,7.f,1.4f,.65f,.9f}};
            const auto& p=presets[id==Choppy?3:id-Calm];s.strength=p[0];s.speed=p[1];s.density=p[2];s.complexity=p[3];s.sharpness=p[4];s.cycling=false;s.clear=false;return Changed;
        }
        if(id>=RowBase&&id<RowEnd){
            const int row=(id-RowBase)/3,part=(id-RowBase)%3;
            const float step=row==1?.5f:row==2?.1f:.05f;
            if(part!=1)set(s,row,value(s,row)+(part==0?-step:step));return Changed;
        }
        return 0;
    }
    void resetInput(){for(auto& h:hands)h={};for(auto& p:pointers)p={};owner=-1;}
    unsigned update(const PanelInput input[2],double now,EffectSettings& s){
        unsigned events=0;
        for(int i=0;i<2;i++){
            auto& h=hands[i];const auto& p=input[i];pointers[i]=p;
            if(!p.tracked){if(owner==i)owner=-1;h={};continue;}
            h.hover=hit(p,open);
            if((h.hover==Mode&&!s.fullViewAvailable)||(s.fullView&&h.hover>=RowBase+15))h.hover=None;
            if(!p.pinch&&!h.down)h.armed=true;
            if(p.pinch&&!h.down){
                h.down=true;
                if(h.armed&&owner<0){
                    int target=h.hover;
                    // Retain a highlighted button through small pinch-induced motion.
                    if(h.previous!=None&&now-h.previousAt<.12&&std::hypot(p.x-h.previousX,p.y-h.previousY)<.04f&&p.surface==(h.previous==Open?1:0))target=h.previous;
                    if(target!=None){h.captured=target;owner=i;}
                }
                h.armed=false;
            }
            if(owner==i&&h.down&&p.pinch&&slider(h.captured)&&p.onSurface&&p.surface==0){
                const int row=(h.captured-RowBase)/3;
                const float t=std::clamp((p.x-trackLeft)/trackWidth,0.f,1.f);
                set(s,row,minimum(row)+(row==1?t*t:t)*(maximum(row)-minimum(row)));events|=Changed;
            }
            if(!p.pinch&&h.down){
                if(owner==i&&h.captured!=None&&!slider(h.captured)&&p.onSurface&&p.surface==(h.captured==Open?1:0)&&rect(h.captured).contains(p.x,p.y,.022f))events|=activate(h.captured,s);
                if(owner==i)owner=-1;
                h.down=false;h.armed=true;h.captured=None;
            }
            if(!p.pinch&&h.hover!=None){h.previous=h.hover;h.previousAt=now;h.previousX=p.x;h.previousY=p.y;}
        }
        return events;
    }
    bool hovered(int id)const{return hands[0].hover==id||hands[1].hover==id;}
    bool pressed(int id)const{return owner>=0&&hands[owner].captured==id;}
};
