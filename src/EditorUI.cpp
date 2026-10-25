#include "EditorUI.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lab {
void ConfigureEditorUIStyle(float scale) {
    ImGui::GetStyle()=ImGuiStyle{};
    ImGui::StyleColorsDark();ImGuiStyle& style=ImGui::GetStyle();
    style.WindowRounding=0;style.ChildRounding=6;style.FrameRounding=4;style.GrabRounding=4;
    style.WindowPadding=ImVec2(12,10);style.ItemSpacing=ImVec2(7,7);style.FramePadding=ImVec2(7,5);
    style.HoverDelayNormal=.6f;
    style.ScaleAllSizes(std::max(.5f,scale));
}
namespace {
void tooltip(const char* text) {
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*24.f);
        ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}
// Sliders are mouse-only. Claim the wheel while hovering so the inspector
// does not scroll at the same time; keyboard navigation cannot change values.
bool wheelSlider(const char* label,float* value,float low,float high,const char* format,float step=0.f) {
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
    bool changed=ImGui::SliderFloat(label,value,low,high,format,ImGuiSliderFlags_NoInput);
    ImGui::PopItemFlag();
    if(ImGui::IsItemHovered()) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        const float wheel=ImGui::GetIO().MouseWheel;
        if(wheel!=0){const float next=std::clamp(*value+wheel*(step>0?step:(high-low)/100.f),low,high);changed|=next!=*value;*value=next;}
    }
    return changed;
}
bool wheelSliderInt(const char* label,int* value,int low,int high) {
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
    bool changed=ImGui::SliderInt(label,value,low,high,"%d",ImGuiSliderFlags_NoInput);
    ImGui::PopItemFlag();
    if(ImGui::IsItemHovered()){ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);const float wheel=ImGui::GetIO().MouseWheel;
        if(wheel!=0){const int next=std::clamp(*value+int(wheel>0?std::ceil(wheel):std::floor(wheel)),low,high);changed|=next!=*value;*value=next;}}
    return changed;
}
void section(const char* title) { ImGui::Spacing();ImGui::SeparatorText(title); }
bool fullButton(const char* label) { return ImGui::Button(label,ImVec2(-1,0)); }
void sourceButton(const char* label,const std::string& name,EditorAction kind,const EditorUICallback& action) {
    if(fullButton(label))action(kind,0);
    if(!name.empty()){ImGui::TextDisabled("%s",name.c_str());tooltip(name.c_str());}
}
void filesPane(EditorUIState& s,const EditorUICallback& action) {
    section("Модель");
    if(fullButton("Открыть OGF"))action(EditorAction::OpenModel,0);
    if(fullButton("Тестовый куб"))action(EditorAction::NewCube,0);
    ImGui::TextWrapped("%s",s.modelName.c_str());
    section("Карты выбранного материала");
    ImGui::BeginDisabled(s.editingLocked);
    sourceButton("Diffuse / цвет",s.sourceNames[0],EditorAction::LoadDiffuse,action);
    sourceButton("Normal / нормали",s.sourceNames[1],EditorAction::LoadNormal,action);
    sourceButton("Gloss / блеск",s.sourceNames[2],EditorAction::LoadGloss,action);
    sourceButton("Height / высота",s.sourceNames[3],EditorAction::LoadHeight,action);
    section("Готовая пара ТЧ");
    sourceButton("Загрузить _bump.dds",s.sourceNames[4],EditorAction::LoadBump,action);
    sourceButton("Загрузить _bump#.dds",s.sourceNames[5],EditorAction::LoadBumpX,action);
    ImGui::TextWrapped("При выборе одного файла парный файл ищется рядом автоматически.");
    section("Сеанс редактирования");
    if(fullButton("Открыть сеанс…"))action(EditorAction::OpenSession,0);
    if(fullButton("Сохранить сеанс…"))action(EditorAction::SaveSession,0);
    ImGui::EndDisabled();
}
void exportPane(EditorUIState& s,const EditorUICallback& action) {
    section("Экспорт для ТЧ");
    ImGui::TextWrapped("Экспортируется текущая пара bump/bump#. Генерация заново не запускается.");
    ImGui::Checkbox("Имя исходной текстуры",&s.useTextureName);
    ImGui::BeginDisabled(s.useTextureName);
    ImGui::SetNextItemWidth(-1);ImGui::InputText("##ExportName",s.exportName.data(),s.exportName.size());
    ImGui::EndDisabled();
    ImGui::BeginDisabled(s.editingLocked);
    if(fullButton("Экспортировать DDS-пару…"))action(EditorAction::Export,0);
    ImGui::EndDisabled();
    ImGui::TextWrapped("Выберите имя и папку в диалоге. Будут записаны <имя>_bump.dds и <имя>_bump#.dds.");
    if(ImGui::Checkbox("Включить Gloss в DDS",&s.glossEnabled))action(EditorAction::GlossSettings,0);
    ImGui::TextWrapped("Отключение обнуляет gloss в превью и экспорте; рабочая карта блеска сохраняется.");
    section("Рабочий сеанс");
    if(fullButton("Сохранить сеанс…"))action(EditorAction::SaveSession,0);
    ImGui::TextWrapped("Сеанс сохраняет исходную базу и нарисованные каналы. История отмены действует в текущем запуске.");
    if(!s.documentInfo.empty())ImGui::TextWrapped("%s",s.documentInfo.c_str());
}
void materialPane(EditorUIState& s,const EditorUICallback& action) {
    section("Материал ТЧ / Kich-Ray R2");
    if(!s.documentInfo.empty())ImGui::TextWrapped("%s",s.documentInfo.c_str());
    bool preview=ImGui::Checkbox("Показывать bump",&s.bump);
    preview|=ImGui::ColorEdit3("Цвет материала (просмотр)",s.tint,ImGuiColorEditFlags_NoInputs);
    if(preview)action(EditorAction::PreviewSettings,0);
    if(ImGui::Checkbox("Gloss: превью и экспорт",&s.glossEnabled))action(EditorAction::GlossSettings,0);
    ImGui::TextWrapped("Gloss поддерживается движком: красный канал _bump.dds, не отдельный игровой файл.");
    section("Правка текущих карт");
    ImGui::BeginDisabled(s.editingLocked||!s.canAdjust);
    bool changed=false;
    changed|=wheelSlider("Сила нормалей",&s.adjustments.normalStrength,0,4,"%.2fx",.05f);
    changed|=wheelSlider("Блеск",&s.adjustments.gloss,0,2,"%.2fx",.02f);
    changed|=wheelSlider("Высота",&s.adjustments.height,0,2,"%.2fx",.02f);
    changed|=wheelSlider("Сглаживание",&s.adjustments.blur,0,1,"%.2f",.02f);
    changed|=wheelSlider("Резкость",&s.adjustments.sharpness,0,2,"%.2f",.02f);
    changed|=ImGui::Checkbox("Инверсия Y",&s.adjustments.invertY);
    if(changed)action(EditorAction::AdjustMaterial,0);
    ImGui::BeginDisabled(!s.pendingAdjustments&&!changed);
    if(fullButton("Применить  Ctrl+S"))action(EditorAction::ApplyAdjustments,0);
    if(fullButton("Отказаться от правок ползунков"))action(EditorAction::DiscardAdjustments,0);
    ImGui::EndDisabled();ImGui::EndDisabled();
    ImGui::TextWrapped("Колесо над ползунком меняет значение. Результат виден сразу. Применение создаёт один шаг Undo; отказ восстанавливает прежние карты и мазки.");
    section("Минимальная база");
    ImGui::BeginDisabled(s.editingLocked);
    if(fullButton("Создать bump…"))action(EditorAction::RequestGenerate,0);
    ImGui::EndDisabled();
    ImGui::TextWrapped("При отсутствии нормалей создаётся мягкая базовая карта. Ползунки регулируют её, не генерируя рельеф заново из diffuse.");
    section("Прозрачность diffuse");
    bool alpha=ImGui::Checkbox("Учитывать альфа-канал",&s.alpha);
    ImGui::BeginDisabled(!s.alpha);
    const char* modes[]={"Авто","Отсечение","Смешивание"};
    alpha|=ImGui::Combo("Режим##Alpha",&s.alphaMode,modes,3);
    ImGui::BeginDisabled(s.alphaMode==2);
    alpha|=wheelSlider("Порог отсечения",&s.alphaCutoff,0,1,"%.3f");
    ImGui::EndDisabled();ImGui::EndDisabled();
    if(alpha)action(EditorAction::AlphaSettings,0);
}
void scenePane(EditorUIState& s,const EditorUICallback& action) {
    section("Освещение");
    bool changed=false;
    changed|=wheelSlider("Азимут",&s.lightAzimuth,0,360,"%.0f°");
    changed|=wheelSlider("Высота света",&s.lightElevation,-80,80,"%.0f°");
    changed|=wheelSlider("Сила света",&s.lightIntensity,0,8,"%.2fx",.05f);
    changed|=ImGui::ColorEdit3("Свет",s.lightColor,ImGuiColorEditFlags_NoInputs);
    changed|=ImGui::ColorEdit3("Окружение",s.ambient,ImGuiColorEditFlags_NoInputs);
    const char* materials[]={"Авто · из THM","0 · Oren–Nayar","1 · Blinn","2 · Phong","3 · Metal"};
    changed|=ImGui::Combo("Класс THM (просмотр)",&s.materialClass,materials,5);
    if(changed)action(EditorAction::LightSettings,0);
    if(ImGui::ColorEdit3("Фон",s.background,ImGuiColorEditFlags_NoInputs))action(EditorAction::BackgroundSettings,0);
    ImGui::TextWrapped("Прямой свет: таблица R2. Класс материала берётся из THM; без THM — Blinn. Погода, окружение уровня и bloom здесь не воспроизводятся.");
    ImGui::BeginDisabled(!s.hasSkeleton);
    if(ImGui::Checkbox("Показывать скелет",&s.skeleton))action(EditorAction::Skeleton,0);
    ImGui::EndDisabled();
    if(fullButton("Сбросить вид"))action(EditorAction::ResetView,0);
    section("Геометрия");
    ImGui::BeginDisabled(s.editingLocked);
    if(fullButton("Показать все меши"))action(EditorAction::ShowAll,0);
    if(fullButton("Снять выделение"))action(EditorAction::ClearSelection,0);
    if(fullButton("Скрыть выбранный"))action(EditorAction::HideSelected,0);
    for(size_t i=0;i<s.meshes.size();++i) {
        auto& mesh=s.meshes[i];ImGui::PushID(int(i));
        if(ImGui::Checkbox("##Visible",&mesh.visible))action(EditorAction::Visibility,int(i));
        ImGui::SameLine();
        if(ImGui::Selectable(mesh.label.c_str(),mesh.selected))action(EditorAction::SelectPart,int(i));
        tooltip(mesh.label.c_str());ImGui::PopID();
    }
    ImGui::EndDisabled();
}
void brushPane(EditorUIState& s,const EditorUICallback& action) {
    section("Кисть материала");
    ImGui::BeginDisabled(!s.canBrush||s.editingLocked);
    if(fullButton(s.mode==EditorMode::Brush?"Кисть включена  [BR]":"Включить кисть  [BR]"))action(EditorAction::SetMode,int(EditorMode::Brush));
    section("Канал");
    const char* channels[]={"Рельеф","Высота","Блеск","Резкость"};
    for(int i=0;i<4;++i){if(i%2)ImGui::SameLine(ImGui::GetWindowContentRegionMax().x*.5f);ImGui::RadioButton(channels[i],&s.brushChannel,i);}
    section("Действие");
    int direction=s.brushRemove?1:0;
    if(ImGui::RadioButton("Добавить (+)",&direction,0))s.brushRemove=false;
    ImGui::SameLine();
    if(ImGui::RadioButton("Убрать (−)",&direction,1))s.brushRemove=true;
    const char* details[]={"Рельеф: добавить детали пресета / сгладить нормали к плоскости.","Высота: поднять / опустить поверхность.","Блеск: усилить / ослабить отражение света.","Резкость: подчеркнуть / смягчить детали нормалей."};
    ImGui::TextWrapped("%s",details[std::clamp(s.brushChannel,0,3)]);
    section("Параметры");
    wheelSlider("Размер",&s.brushRadiusPercent,.1f,20.f,"%.1f%%");
    wheelSlider("Сила",&s.brushStrengthPercent,0,100,"%.2f%%");
    wheelSlider("Резкость края",&s.brushHardnessPercent,0,100,"%.0f%%");
    section("Пресеты нормалей");
    if(ImGui::BeginTabBar("Brush presets")) {
        if(ImGui::BeginTabItem("Материалы")) {
            const char* names[]={"Стекло","Ткань","Металл","Бетон","Кожа"};
            for(int i=0;i<5;++i){if(ImGui::Selectable(names[i],s.surfacePreset==i))action(EditorAction::SurfacePreset,i);}
            ImGui::TextWrapped("Процедурный микрорельеф. Стекло — слабая волнистость, ткань — переплетение, металл — бороздки, бетон — зерно, кожа — поры. Прозрачность и тип шейдера не меняются.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Из файла")) {
            if(fullButton("Загрузить пресет…"))action(EditorAction::LoadPreset,0);
            if(fullButton("Сохранить пресет…"))action(EditorAction::SavePreset,0);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::TextWrapped("Выбран: %s",s.presetName.c_str());
    section("История");
    ImGui::BeginDisabled(!s.canUndo);if(fullButton("Отменить  Ctrl+Z"))action(EditorAction::Undo,0);ImGui::EndDisabled();
    ImGui::BeginDisabled(!s.canRedo);if(fullButton("Повторить  Ctrl+Y"))action(EditorAction::Redo,0);ImGui::EndDisabled();
    if(fullButton("Очистить слой (можно отменить)"))action(EditorAction::ClearBrush,0);
    if(fullButton("Сохранить сеанс…"))action(EditorAction::SaveSession,0);
    ImGui::EndDisabled();
    if(!s.canBrush)ImGui::TextWrapped("Сначала загрузите готовую пару bump/bump# или создайте базу.");
    ImGui::TextWrapped("ЛКМ — мазок; ПКМ — камера; колесо над моделью — размер кисти. Сила и край — колесом над ползунками. Новый мазок накапливается поверх предыдущего.");
}
void animationPane(EditorUIState& s,const EditorUICallback& action) {
    section("Анимация");
    if(s.animations.empty()){ImGui::TextWrapped("У модели нет совместимых анимаций. OMF ищется рядом с OGF.");return;}
    if(ImGui::BeginCombo("Клип",s.animations[size_t(std::clamp(s.animationIndex,0,int(s.animations.size())-1))].c_str(),ImGuiComboFlags_HeightLarge)) {
        const bool opening=ImGui::IsWindowAppearing();
        for(size_t i=0;i<s.animations.size();++i){ImGui::PushID(int(i));const bool selected=int(i)==s.animationIndex;
            if(ImGui::Selectable(s.animations[i].c_str(),selected)){s.animationIndex=int(i);action(EditorAction::AnimationSelect,int(i));}
            if(selected){ImGui::SetItemDefaultFocus();if(opening)ImGui::SetScrollHereY(.5f);}ImGui::PopID();}
        ImGui::EndCombo();
    }
    if(fullButton(s.animationPlaying?"Пауза":"Старт"))action(EditorAction::AnimationPlay,0);
    bool settings=ImGui::Checkbox("Повторять",&s.animationLoop);
    settings|=wheelSlider("Скорость",&s.animationSpeed,.25f,2.f,"%.2fx");
    if(settings)action(EditorAction::AnimationSettings,0);
    if(wheelSliderInt("Кадр",&s.animationFrame,0,std::max(0,s.animationFrames-1)))action(EditorAction::AnimationSeek,0);
    ImGui::TextWrapped("Во время воспроизведения редактирование материала заблокировано. При уходе со вкладки модель возвращается в исходную позу.");
}
void toolbar(EditorUIState& s,const EditorUICallback& action) {
    struct Tool{const char* name;const char* help;EditorAction action;int arg;bool active;bool enabled;};
    const Tool tools[]={
        {"FC","Полёт камеры (1). WASD/QE; Shift быстрее; Ctrl точнее. ЛКМ/ПКМ — осмотр.",EditorAction::SetMode,0,s.mode==EditorMode::Fly,true},
        {"OC","Орбита (2). ЛКМ — камера; СКМ — панорама; колесо — масштаб.",EditorAction::SetMode,1,s.mode==EditorMode::Orbit,true},
        {"CM","Выбрать меш (3/C). ЛКМ — выбрать; ПКМ — снять выделение.",EditorAction::SetMode,2,s.mode==EditorMode::Choose,!s.editingLocked},
        {"OL","Контур выбранного меша (O).",EditorAction::Outline,0,s.outline,true},
        {"RL","Вращение света (4). ЛКМ — свет; ПКМ — камера.",EditorAction::SetMode,3,s.mode==EditorMode::Light,true},
        {"BR","Кисть (5/B). ЛКМ — мазок; ПКМ — камера; колесо — размер.",EditorAction::SetMode,4,s.mode==EditorMode::Brush,s.canBrush&&!s.editingLocked},
        {"RM","Вращение модели (6/R). ЛКМ — модель; ПКМ — камера.",EditorAction::SetMode,5,s.mode==EditorMode::Model,!s.editingLocked&&s.tab!=EditorTab::Animation},
        {"FT","Вместить выбранный меш или всю видимую модель (F).",EditorAction::Frame,0,false,true},
        {"IS","Изолировать выделение; повторное нажатие вернёт видимость (I).",EditorAction::Isolate,0,s.isolated,!s.editingLocked},
        {"WF","Каркас треугольников (V).",EditorAction::Wireframe,0,s.wireframe,true},
        {"UN","Отменить (Ctrl+Z).",EditorAction::Undo,0,false,s.canUndo&&!s.editingLocked},
        {"RE","Повторить (Ctrl+Y).",EditorAction::Redo,0,false,s.canRedo&&!s.editingLocked},
        {"AP","Применить правки ползунков (Ctrl+S). DDS экспортируется отдельно.",EditorAction::ApplyAdjustments,0,false,s.pendingAdjustments&&!s.editingLocked},
        {"RS","Сбросить камеру и положение модели (Home).",EditorAction::ResetView,0,false,true},
        {"Z","Вместить модель: вид по оси Z.",EditorAction::AxisView,2,false,true},
        {"X","Вместить модель: вид по оси X.",EditorAction::AxisView,0,false,true},
        {"Y","Вместить модель: вид по оси Y.",EditorAction::AxisView,1,false,true}
    };
    const float width=ImGui::GetContentRegionAvail().x;
    const float button=std::max(30.f*s.uiScale,ImGui::CalcTextSize("WW").x+ImGui::GetStyle().FramePadding.x*2);
    const float gap=ImGui::GetStyle().ItemSpacing.x;
    const int columns=std::max(1,int((width+gap)/(button+gap)));
    for(int i=0;i<int(sizeof(tools)/sizeof(tools[0]));++i) {
        if(i%columns)ImGui::SameLine();const auto& t=tools[i];
        ImGui::BeginDisabled(!t.enabled);
        if(t.active)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.13f,.38f,.60f,1));
        if(ImGui::Button(t.name,ImVec2(button,0)))action(t.action,t.arg);
        if(t.active)ImGui::PopStyleColor();ImGui::EndDisabled();tooltip(t.help);
    }
}
void generatePopup(EditorUIState& s,const EditorUICallback& action) {
    if(s.requestGeneratePopup){ImGui::OpenPopup("Создать новую базу");s.requestGeneratePopup=false;}
    const ImVec2 display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(560.f*s.uiScale,display.x-32.f),std::min(520.f*s.uiScale,display.y-32.f)),ImGuiCond_Always);
    if(ImGui::BeginPopupModal("Создать новую базу",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("Выберите материалы. Будет создан минимальный мягкий пресет. Существующие мазки выбранных материалов будут заменены новой базой.");
        if(s.generateTargets.size()!=s.meshes.size())s.generateTargets.assign(s.meshes.size(),false);
        ImGui::BeginChild("Generate targets",ImVec2(0,std::max(40.f,ImGui::GetContentRegionAvail().y-ImGui::GetFrameHeightWithSpacing()*2.f)),ImGuiChildFlags_Borders);
        for(size_t i=0;i<s.meshes.size();++i){ImGui::PushID(int(i));bool chosen=s.generateTargets[i];ImGui::BeginDisabled(!s.meshes[i].hasDiffuse||!s.meshes[i].visible);if(ImGui::Checkbox(s.meshes[i].label.c_str(),&chosen))s.generateTargets[i]=chosen;ImGui::EndDisabled();ImGui::PopID();}
        ImGui::EndChild();
        if(ImGui::Button("Все доступные"))for(size_t i=0;i<s.meshes.size();++i)s.generateTargets[i]=s.meshes[i].hasDiffuse&&s.meshes[i].visible;
        if(ImGui::Button("Создать")){action(EditorAction::GenerateSelected,0);ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Отмена"))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
}
void DrawEditorUI(EditorUIState& s,const EditorUICallback& action) {
    ImGuiIO& io=ImGui::GetIO();s.renderedPaneCount=0;s.viewportHovered=false;s.viewportClicked=false;
    ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Material Lab workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextUnformatted("KICH-RAY  /  MATERIAL LAB");ImGui::SameLine();ImGui::TextDisabled("0.11.0");
    ImGui::Separator();
    const float statusHeight=ImGui::GetTextLineHeightWithSpacing()*2.f;
    const ImVec2 room=ImGui::GetContentRegionAvail();
    const float sideWidth=std::clamp(410.f*s.uiScale,250.f,std::max(250.f,room.x*.48f));
    const float bodyHeight=std::max(120.f,room.y-statusHeight);
    ImGui::BeginChild("Inspector",ImVec2(sideWidth,bodyHeight),ImGuiChildFlags_Borders);
    const ImVec2 inspector=ImGui::GetWindowPos();s.inspectorRect={inspector.x,inspector.y,ImGui::GetWindowWidth(),ImGui::GetWindowHeight()};
    const char* tabs[]={"Файлы","Экспорт","Материал","Сцена","Кисть","Анимация"};
    const int requestedTab=int(s.tab);
    if(ImGui::BeginTabBar("Editor tabs",ImGuiTabBarFlags_FittingPolicyScroll)) {
        for(int i=0;i<6;++i) {
            const ImGuiTabItemFlags flags=s.forceTab&&requestedTab==i?ImGuiTabItemFlags_SetSelected:0;
            if(ImGui::BeginTabItem(tabs[i],nullptr,flags)) {
                s.tab=EditorTab(i);++s.renderedPaneCount;
                ImGui::PushID(i);
                ImGui::BeginChild("Active pane",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_AlwaysVerticalScrollbar);
                ImGui::PushItemWidth(-std::min(140.f*s.uiScale,ImGui::GetContentRegionAvail().x*.43f));
                switch(s.tab){case EditorTab::Files:filesPane(s,action);break;case EditorTab::Export:exportPane(s,action);break;case EditorTab::Material:materialPane(s,action);break;case EditorTab::Scene:scenePane(s,action);break;case EditorTab::Brush:brushPane(s,action);break;case EditorTab::Animation:animationPane(s,action);break;}
                ImGui::PopItemWidth();ImGui::EndChild();ImGui::PopID();ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    s.forceTab=false;ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("Viewport and tools",ImVec2(0,bodyHeight),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const float button=std::max(30.f*s.uiScale,ImGui::CalcTextSize("WW").x+ImGui::GetStyle().FramePadding.x*2);
    const float available=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemSpacing.x;
    const int columns=std::max(1,int((available+gap)/(button+gap))),rows=(17+columns-1)/columns;
    const float toolHeight=rows*ImGui::GetFrameHeightWithSpacing()+ImGui::GetTextLineHeightWithSpacing()*2;
    const ImVec2 viewPos=ImGui::GetCursorScreenPos();
    const ImVec2 viewSize(std::max(1.f,available),std::max(1.f,ImGui::GetContentRegionAvail().y-toolHeight));
    s.viewportRect={viewPos.x,viewPos.y,viewSize.x,viewSize.y};
    ImGui::InvisibleButton("Viewport input",viewSize,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);
    s.viewportHovered=ImGui::IsItemHovered();s.viewportClicked=ImGui::IsItemClicked(0)||ImGui::IsItemClicked(1)||ImGui::IsItemClicked(2);
    ImDrawList* draw=ImGui::GetWindowDrawList();
    if(s.viewportTexture)draw->AddImage(s.viewportTexture,viewPos,ImVec2(viewPos.x+viewSize.x,viewPos.y+viewSize.y));
    else draw->AddRectFilled(viewPos,ImVec2(viewPos.x+viewSize.x,viewPos.y+viewSize.y),IM_COL32(24,27,31,255));
    const ImVec2 toolPos=ImGui::GetCursorScreenPos();s.toolbarRect={toolPos.x,toolPos.y,available,toolHeight};
    toolbar(s,action);
    static const char* modeNames[]={"Полёт камеры","Орбита","Выбор меша","Вращение света","Кисть","Вращение модели"};
    ImGui::TextDisabled("%s%s",modeNames[int(s.mode)],s.hasEdits?"  ·  материал изменён":"");
    ImGui::EndChild();
    ImGui::Separator();ImGui::TextWrapped("%s",s.status.c_str());
    generatePopup(s,action);ImGui::End();
}
}
