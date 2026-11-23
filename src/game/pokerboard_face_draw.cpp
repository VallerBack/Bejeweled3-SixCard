#include "poker_context.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <initializer_list>

#include "poker_sidecar.h"
#include "poker_rules.h"
#include "poker_table_model.h"
#include "poker_partial_hand.h"
#include "sixth_acquisition.h"
extern "C" BOOL __stdcall BJ3PublishSixthBonusToTotal(uint32_t,uint32_t) noexcept;
extern "C" BOOL __stdcall BJ3CopyActiveHandForDisplay(uint32_t, uint32_t, bj3six::HandSnapshot*) noexcept;

extern "C" BOOL __stdcall BJ3CopyClassifiedSixthCardForDraw(
    uint32_t board, uint32_t board_vptr, uint32_t* generation,
    int32_t* gem_value, uint32_t* category) noexcept;

extern "C" __declspec(dllexport) volatile LONG g_BJ3PresentationAlpha = 255;
extern "C" __declspec(dllexport) volatile LONG g_BJ3PresentationReveal = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3PresentationTitleLeft = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3PresentationTitleWidth = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3PresentationBorders = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3PresentationBonusLabels = 0;

namespace {

const uint32_t kImageBase = 0x00400000;
const uint32_t kPokerDrawSlotAddress = 0x008118C8;
const uint32_t kPokerDrawOriginal = 0x006C4A00;
const uint32_t kPokerDrawRva = 0x002C4A00;
const uint32_t kAppGlobal = 0x00887F34;
const uint32_t kCardsFrontGlobal = 0x0087D5AC;
const uint32_t kCardsFaceGlobal = 0x0087E4D0;
const uint32_t kGraphicsFontOffset = 0x68;
const uint32_t kGraphicsDrawString = 0x004309B0;
const uint32_t kImageAtlasDraw = 0x004327B0;
const uint32_t kGlobalImageFont = 0x008883D4;
const uint32_t kImageFontVtable = 0x0081D4E4;
// Run260 measured the original five card-front destinations at x=347,417,
// 487,557,627; y=256; size=59x82 in the pinned 800x600 logical canvas.
// The user design places card six immediately to their right.
const int32_t kFirstCardX = 312;
const int32_t kCardStep = 70;
const int32_t kSixthCardX = 697;
const int32_t kSixthCardY = 256;
const int32_t kCardWidth = 59;
const int32_t kCardHeight = 82;
const int32_t kCategoryLabelX = 312;
const int32_t kCategoryLabelY = 218;

struct TargetSexyStringView {
    uint32_t reserved;
    union Storage {
        uint16_t inline_characters[8];
        const uint16_t* heap_data;
    } storage;
    uint32_t length;
    uint32_t capacity;
};

static_assert(sizeof(TargetSexyStringView) == 0x1C,
    "target x86 SexyString storage must remain 0x1C bytes");
static_assert(offsetof(TargetSexyStringView, storage) == 0x04,
    "target string storage offset changed");
static_assert(offsetof(TargetSexyStringView, length) == 0x14,
    "target string length offset changed");
static_assert(offsetof(TargetSexyStringView, capacity) == 0x18,
    "target string capacity offset changed");

struct FaceDestination {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

struct DrawTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t graphics;
    uint32_t native_return;
    uint32_t fixture_state; // 3 means a classified sidecar supplied the face.
    uint32_t face_drawn;
    uint32_t frame;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t generation;
    int32_t gem_value;
    uint32_t category;
    uint32_t label_drawn;
    uint32_t graphics_font;
    int32_t label_x;
    int32_t label_y;
};

struct PreviewTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t graphics;
    uint32_t faces_drawn;
    uint32_t category;
    uint32_t label_drawn;
    uint32_t graphics_font;
    int32_t gem_values[6];
    int32_t rectangles[24];
};

struct PreviewDrawResult {
    uint32_t faces_drawn;
    uint32_t category;
    uint32_t label_drawn;
    uint32_t graphics_font;
    int32_t gem_values[6];
    int32_t rectangles[24];
};




volatile LONG g_hook_state = 0;

void DrawAtlasCard(void* image, void* graphics, int32_t gem,
        FaceDestination* destination) noexcept {
    const uint32_t frame = static_cast<uint32_t>(gem + 1);
    __asm {
        mov ecx, image
        mov eax, frame
        mov edx, destination
        push edx
        mov edx, graphics
        push edx
        mov edx, kImageAtlasDraw
        call edx
    }
}

bool DrawStringAt(void* graphics, const char* name, int32_t x, int32_t y,
        uint32_t* font_output, bool centered = false, bool right_aligned = false) noexcept {
    if (graphics == nullptr || name == nullptr || font_output == nullptr) return false;
    const uint32_t graphics_address = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(graphics));
    const uint32_t font = *reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(graphics_address + kGraphicsFontOffset));
    *font_output = font;
    if (font == 0) return false;

    uint16_t text[32] = {};
    uint32_t length = 0;
    while (length < 31 && name[length] != '\0') {
        text[length] = static_cast<uint8_t>(name[length]);
        ++length;
    }
    if (length == 0) return false;

    TargetSexyStringView view = {};
    view.length = length;
    if (length < 8) {
        for (uint32_t index = 0; index != length; ++index)
            view.storage.inline_characters[index] = text[index];
        view.capacity = 7;
    } else {
        view.storage.heap_data = text;
        view.capacity = length;
    }

    if(centered || right_aligned) {
        typedef int32_t (__thiscall *Measure)(void*,const TargetSexyStringView*);
        const uint32_t table=*reinterpret_cast<const uint32_t*>(font);
        Measure measure=reinterpret_cast<Measure>(*reinterpret_cast<const uint32_t*>(table+0x1C));
        const int32_t width=measure(reinterpret_cast<void*>(font),&view);
        x-=right_aligned ? width:width/2;
        if(centered) {
            g_BJ3PresentationTitleLeft=x;
            g_BJ3PresentationTitleWidth=width;
        }
    }
    void* view_address = &view;
    __asm {
        mov eax, graphics
        push y
        push x
        push view_address
        mov edx, kGraphicsDrawString
        call edx
    }
    return true;
}

int32_t LayoutHeight() noexcept {
    const uint32_t app=*reinterpret_cast<const uint32_t*>(kAppGlobal);
    if(!app) return 600;
    const int32_t height=*reinterpret_cast<const int32_t*>(app+0x9E0);
    return height>0 ? height : 600;
}
int32_t Layout(int32_t value,int32_t height) noexcept {
    return static_cast<int32_t>((static_cast<int64_t>(value)*height)/600);
}

struct PresentationColor {int32_t r,g,b,a;};
void SetPresentationColor(void* graphics,const PresentationColor& color) noexcept {
    const void* data=&color;
    __asm { mov ecx,graphics
        mov eax,data
        mov edx,0042FF00h
        call edx }
}
float HandProgress(void* board) noexcept {
    typedef double (__thiscall *Curve)(void*);
    const double value=reinterpret_cast<Curve>(0x00435510u)(static_cast<uint8_t*>(board)+0x3718);
    if(!(value>=0.0)) return 0.0f;
    return value>1.0 ? 1.0f : static_cast<float>(value);
}
bool DrawSidecarFace(void* board, void* graphics, FaceDestination* destination,
        uint32_t* frame_output, uint32_t* generation_output,
        int32_t* gem_output, uint32_t* category_output) noexcept {
    if (board == nullptr || graphics == nullptr || destination == nullptr ||
        frame_output == nullptr || generation_output == nullptr || gem_output == nullptr ||
        category_output == nullptr)
        return false;

    const uint32_t board_address = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(board));
    const uint32_t board_vptr = *reinterpret_cast<const uint32_t*>(board);
    uint32_t generation = 0;
    int32_t gem = -1;
    uint32_t category = 0;
    if (!BJ3CopyClassifiedSixthCardForDraw(board_address, board_vptr,
            &generation, &gem, &category)) return false;

    void* image = *reinterpret_cast<void**>(kCardsFaceGlobal);
    const uint32_t app = *reinterpret_cast<const uint32_t*>(kAppGlobal);
    if (image == nullptr || app == 0) return false;

    destination->x = kSixthCardX;
    destination->y = kSixthCardY;

    const uint8_t* image_bytes = reinterpret_cast<const uint8_t*>(image);
    const uint32_t image_columns = *reinterpret_cast<const uint32_t*>(image_bytes + 0x38);
    const uint32_t image_rows = *reinterpret_cast<const uint32_t*>(image_bytes + 0x34);
    if (image_columns == 0 || image_rows == 0 || gem < 0 || gem > 6)
        return false;
    destination->width = kCardWidth;
    destination->height = kCardHeight;
    const uint32_t frame = static_cast<uint32_t>(gem + 1);

    // Atlas helper ABI: ECX=image, EAX=frame, stack=[Graphics, rectangle].
    DrawAtlasCard(image, graphics, gem, destination);

    *frame_output = frame;
    *generation_output = generation;
    *gem_output = gem;
    *category_output = category;
    return true;
}

bool DrawCategoryLabel(void* graphics, uint32_t category,
        uint32_t* font_output) noexcept {
    const char* name = bj3six::CategoryName(
        static_cast<bj3six::Category>(category));
    return DrawStringAt(graphics, name, kCategoryLabelX, kCategoryLabelY,
        font_output);
}

} // namespace

extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardUiPreviewEnabled = 0;

extern "C" __declspec(dllexport) PreviewTelemetry
    g_BJ3SixCardUiPreviewLastCall = {};

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3SetSixCardUiPreviewEnabled(LPVOID enabled) noexcept {
    (void)enabled;
    InterlockedExchange(&g_BJ3SixCardUiPreviewEnabled, 0);
    return 1;
}

extern "C" __declspec(dllexport) DrawTelemetry g_BJ3SixthFaceDrawLastCall = {};

extern "C" __declspec(dllexport) float g_BJ3RealHandPriorTransform[4] = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandLayoutHeight = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandRectangles[24] = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandFrontCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandDrawCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandLastGeneration = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandLastCategory = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandLastFaces = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3RealHandLastValues[6] = {};


// Separate, borrowed render records. These are not the native five-row table.
// Curve objects and first-five PIEffects remain owned by the live Board.
#include "native_hand_bytes.inc"
static unsigned char g_render_rows[6*0xF8]={};
static const double g_collector_card_step=48.0;
static bool NativeCurveActive(void* curve) noexcept;
static unsigned char g_sixth_acquisition_curve[0x70]={};
static bool g_sixth_acquisition_initialized=false;
static unsigned char g_sixth_closed_curve[0x70]={};
static bool g_sixth_closed_initialized=false;
static uint32_t g_acquisition_board=0,g_acquisition_generation=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthAcquisitionStarts=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthGemFlights=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthAcquisitionTarget[2]={};
extern "C" uint32_t __stdcall BJ3SixthAcquisitionPending(void* board) noexcept {
    return g_sixth_acquisition_initialized && g_acquisition_board==reinterpret_cast<uint32_t>(board) &&
        NativeCurveActive(g_sixth_acquisition_curve);
}
extern "C" void __stdcall BJ3BeginSixthAcquisition(uint32_t board,uint32_t generation,
        const SixthGemSource* source,int32_t resolved_color) noexcept {
    if(!board || !source || (board==g_acquisition_board && generation==g_acquisition_generation))return;
    if(!g_sixth_acquisition_initialized) {
        typedef void (__thiscall *Construct)(void*);
        reinterpret_cast<Construct>(0x00434240u)(g_sixth_acquisition_curve);
        g_sixth_acquisition_initialized=true;
    }
    void* curve=g_sixth_acquisition_curve;
    uint8_t native_string[0x1C]={};void* text=native_string;
    const char* encoded=reinterpret_cast<const char*>(0x007A8E7Cu);
    __asm {
        push encoded
        mov ecx,text
        mov edx,00401240h
        call edx
        push 0
        push text
        mov eax,curve
        mov edx,00434FA0h
        call edx
        mov ecx,text
        mov edx,004012B0h
        call edx
    }
    g_acquisition_board=board;g_acquisition_generation=generation;++g_BJ3SixthAcquisitionStarts;
    const uint32_t manager=*reinterpret_cast<uint32_t*>(board+0x12C0);
    void* front=*reinterpret_cast<void**>(kCardsFrontGlobal);
    if(!manager || !front)return;
    typedef int32_t (__thiscall *Dimension)(void*);
    typedef int32_t (__thiscall *BoardDimension)(void*);
    const uint32_t vptr=*reinterpret_cast<uint32_t*>(board);
    const int32_t width=reinterpret_cast<BoardDimension>(*reinterpret_cast<uint32_t*>(vptr+0x3E0))(reinterpret_cast<void*>(board));
    const double scale=static_cast<double>(LayoutHeight())/1200.0;
    const int32_t x=width-180+5*48+static_cast<int32_t>(reinterpret_cast<Dimension>(0x00459620u)(front)/2/scale);
    const int32_t y=*reinterpret_cast<int32_t*>(board+0x3870)+5*7+static_cast<int32_t>(reinterpret_cast<Dimension>(0x00459610u)(front)/2/scale);
    g_BJ3SixthAcquisitionTarget[0]=x;g_BJ3SixthAcquisitionTarget[1]=y;
    for(uint32_t flight=0;flight<(source->dual ? 2u:1u);++flight) {
    const float origin_x=flight ? source->alternate_x:source->x;
    const float origin_y=flight ? source->alternate_y:source->y;
    const int32_t source_color=flight ? source->alternate_color:source->color;
    uint8_t candidate[0x22C]={};
    memcpy(candidate,&board,4);memcpy(candidate+0x10,&origin_x,4);memcpy(candidate+0x14,&origin_y,4);
    const int32_t color=source_color>=0 && source_color<7 ? source_color:resolved_color;
    memcpy(candidate+0x220,&color,4);
    typedef void* (__cdecl *Allocate)(size_t);
    void* effect=reinterpret_cast<Allocate>(0x0064337Fu)(0x1E8);
    if(!effect)continue;
    void* captured=candidate;
    __asm {
        push y
        push x
        push effect
        mov edi,captured
        mov eax,006FF550h
        call eax
        mov effect,eax
    }
    uint32_t pair[2]={*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(effect)+8),reinterpret_cast<uint32_t>(effect)};
    uint32_t iterator[2]={};void* item=pair;void* destination=iterator;const uint32_t map=manager+0xBC;
    __asm {
        mov eax,map
        mov ebx,item
        mov esi,destination
        mov edx,00419360h
        call edx
    }
    *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(effect)+0x1B8)=manager;
    typedef void (__stdcall *AttachTrail)(void*,void*);
    reinterpret_cast<AttachTrail>(0x00700520u)(effect,candidate);
    ++g_BJ3SixthGemFlights;
    }
}
static void* g_native_six_renderer=nullptr;
static void* g_sixth_effect=nullptr;
static uint32_t g_effect_board=0,g_effect_generation=0;
static bool g_sixth_popup_emitted=false;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthNativePopups=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthPopupRequestedAmount=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthPopupMergeKey=-1;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthPopupGeneration=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthPulseStarts=0;
static void StartNativeSixthBonusPulse(void* board) noexcept {
    // Exact native curve and string lifetime from 0x006C4588..0x006C45C2.
    // The native sequence has already stopped at index5, so its update loop
    // will not read a sixth native record. It still ticks +3790 each frame.
    uint8_t native_string[0x1C]={};
    void* text=native_string;
    void* curve=static_cast<uint8_t*>(board)+0x3790;
    const char* encoded=reinterpret_cast<const char*>(0x007A9188u);
    __asm {
        push encoded
        mov ecx,text
        mov edx,00401240h
        call edx
        push 0
        push text
        mov eax,curve
        mov edx,00434FA0h
        call edx
        mov ecx,text
        mov edx,004012B0h
        call edx
    }
    ++g_BJ3SixthPulseStarts;
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3NativeSixDraws=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthFlameCreates=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthLightningCreates=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthEffectUpdates=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthEffectReleases=0;
static void ReleaseSixthEffect() noexcept {
    if(g_sixth_effect) {
        typedef void (__stdcall *Destroy)(void*);
        reinterpret_cast<Destroy>(0x00561840u)(g_sixth_effect);
        typedef void (__cdecl *Free)(void*);
        reinterpret_cast<Free>(0x006432A6u)(g_sixth_effect);
        g_sixth_effect=nullptr;
        ++g_BJ3SixthEffectReleases;
    }
    g_effect_board=0;g_effect_generation=0;g_sixth_popup_emitted=false;
}
extern "C" void __stdcall BJ3DiscardSixthPresentation(void* board) noexcept {
    if(g_effect_board==reinterpret_cast<uint32_t>(board))ReleaseSixthEffect();
    if(g_acquisition_board==reinterpret_cast<uint32_t>(board)){g_acquisition_board=0;g_acquisition_generation=0;}
}
extern "C" void __stdcall BJ3UpdateSixthPresentation(void* board) noexcept {
    if(g_sixth_acquisition_initialized && g_acquisition_board==reinterpret_cast<uint32_t>(board)) {
        typedef void (__thiscall *Tick)(void*);
        const uint8_t* state=static_cast<uint8_t*>(board);
        const uint32_t app=*reinterpret_cast<uint32_t*>(kAppGlobal);
        if(!state[0x2978] && !state[0x1414] && !state[0x12C4] && app &&
            *reinterpret_cast<uint32_t*>(app+0x378)==0)
            reinterpret_cast<Tick>(0x004356B0u)(g_sixth_acquisition_curve);
        if(*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(board)+0x3710)==0) {
            g_acquisition_board=0;g_acquisition_generation=0;
        }
    }
    bj3six::HandSnapshot hand={};
    const uint32_t address=reinterpret_cast<uint32_t>(board);
    const bool active=board && *reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(board)+0x3710)==5 &&
        BJ3CopyActiveHandForDisplay(address,*reinterpret_cast<const uint32_t*>(board),&hand);
    if(!active || g_effect_board!=address || g_effect_generation!=hand.generation) {
        ReleaseSixthEffect();
        if(!active) return;
        g_effect_board=address;g_effect_generation=hand.generation;
    }
    const uint32_t type=hand.display_types[5];
    if(type!=2 && type!=3) return;
    // Native bonus sequence finishes the five native rows at index >=5.
    // Emit one visual popup only; score credit remains the existing once-only path.
    if(!g_sixth_popup_emitted && *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(board)+0x3788)==5) {
        void* list=*reinterpret_cast<void**>(static_cast<uint8_t*>(board)+0x1268);
        if(list && hand.bonus_values[5]>0) {
            const int32_t color=hand.resolved_values[5];
            if(color>=0 && color<=6 && BJ3PublishSixthBonusToTotal(hand.board,hand.generation)) {
                const PresentationColor rgba=*reinterpret_cast<const PresentationColor*>(0x008884C8u+color*16);
                StartNativeSixthBonusPulse(board);
                typedef void* (__thiscall *Popup)(void*,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,PresentationColor);
                g_BJ3SixthPopupRequestedAmount=hand.bonus_values[5];
                g_BJ3SixthPopupGeneration=hand.generation;
                g_BJ3SixthPopupMergeKey=-1;
                // Native -1 merge key bypasses lookup; it is not a card index.
                reinterpret_cast<Popup>(0x006F2A70u)(list,
                    Layout(kFirstCardX+5*kCardStep+kCardWidth/2,LayoutHeight()),
                    Layout(kSixthCardY+kCardHeight/2,LayoutHeight()),hand.bonus_values[5],-1,0,-1,0,rgba);
                g_sixth_popup_emitted=true;++g_BJ3SixthNativePopups;
            }
        }
    }
    if(g_sixth_effect) {
        typedef void (__stdcall *Update)(void*);
        reinterpret_cast<Update>(0x0056D500u)(g_sixth_effect);
        ++g_BJ3SixthEffectUpdates;
    } else {
        // Same template and constructor as native 0x006C3F68..0x006C3FA4.
        typedef void* (__stdcall *Create)(void*);
        void* prototype=*reinterpret_cast<void**>(type==2 ? 0x0087F9ECu : 0x0087DAB4u);
        if(!prototype) return;
        g_sixth_effect=reinterpret_cast<Create>(0x00561A20u)(prototype);
        if(g_sixth_effect) {
            *(static_cast<uint8_t*>(g_sixth_effect)+0xF5A)=1;
            if(type==2) ++g_BJ3SixthFlameCreates;
            else ++g_BJ3SixthLightningCreates;
        }
    }
}
static bool InitNativeSixRenderer() noexcept {
    if(g_native_six_renderer) return true;
    // Verify all copied code against this pinned process before deriving it.
    if(memcmp(reinterpret_cast<const void*>(0x006BBE90u),kNativeHandBytes,sizeof(kNativeHandBytes))) return false;
    unsigned char* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,sizeof(kNativeHandBytes),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code) return false;
    memcpy(code,kNativeHandBytes,sizeof(kNativeHandBytes));
    for(unsigned n=0;n<sizeof(kNativeExternalRel32)/sizeof(unsigned);++n) {
        const unsigned offset=kNativeExternalRel32[n];int32_t rel=0;
        memcpy(&rel,code+offset,4);
        rel+=static_cast<int32_t>(0x006BBE90u-reinterpret_cast<uint32_t>(code));
        memcpy(code+offset,&rel,4);
    }
    // Replace LEA ESI,[EDI+3248] with PUSH render_rows+10 / POP ESI.
    const uint32_t rows=reinterpret_cast<uint32_t>(g_render_rows)+0x10;
    code[0x3D]=0x68;memcpy(code+0x3E,&rows,4);code[0x42]=0x5E;
    // The native loop's EBX advances seven per card: 35->42 means six rows.
    code[0x006BCC41u-0x006BBE90u+2]=0x2A;
    // Collector span: six slots use 5*48, matching the old 4*60 span.
    // The separate central-settlement step remains unchanged.
    const uint32_t collector_step=reinterpret_cast<uint32_t>(&g_collector_card_step);
    memcpy(code+(0x006BC007u-0x006BBE90u)+2,&collector_step,4);
    memcpy(code+(0x006BC0ADu-0x006BBE90u)+2,&collector_step,4);
    const int32_t final_x=0x89; memcpy(code+(0x006BC0E1u-0x006BBE90u)+1,&final_x,4);
    DWORD old=0;
    if(!VirtualProtect(code,sizeof(kNativeHandBytes),PAGE_EXECUTE_READ,&old)) {VirtualFree(code,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),code,sizeof(kNativeHandBytes));
    g_native_six_renderer=code;
    return true;
}
static bool DrawNativeSixHand(void* board,void* graphics,const bj3six::HandSnapshot& hand) noexcept {
    if(!InitNativeSixRenderer()) return false;
    memcpy(g_render_rows,static_cast<unsigned char*>(board)+0x3238,5*0xF8);
    // Sixth owns its value/effect; it borrows only row four's settled acquisition
    // curve. The shared hand curve still supplies native settlement flight/fade.
    memcpy(g_render_rows+5*0xF8,g_render_rows+4*0xF8,0xF8);
    if(g_sixth_acquisition_initialized && g_acquisition_board==hand.board && g_acquisition_generation==hand.generation)
        memcpy(g_render_rows+5*0xF8+0x10,g_sixth_acquisition_curve,0x70);
    for(unsigned i=0;i<6;++i) {
        int32_t* row=reinterpret_cast<int32_t*>(g_render_rows+i*0xF8);
        row[0]=hand.resolved_values[i];
        // Native renderer draws every nonnegative secondary value, even when
        // equal to primary. Candidate metadata may repeat primary in +21C.
        const int32_t primary=hand.cards[i].primary_value, alternate=hand.cards[i].alternate_value;
        row[1]=(hand.display_types[i]==1 || alternate<0 || alternate==primary) ? -2 :
            (hand.resolved_values[i]==alternate ? primary:alternate);
        row[2]=static_cast<int32_t>(hand.display_types[i]);
    }
    *reinterpret_cast<void**>(g_render_rows+5*0xF8+0xF0)=
        g_effect_board==hand.board && g_effect_generation==hand.generation ? g_sixth_effect : nullptr;
    typedef void (__stdcall *Draw)(void*,void*);
    reinterpret_cast<Draw>(g_native_six_renderer)(board,graphics);
    ++g_BJ3NativeSixDraws;
    // The native +AC renderer supplies the animated title later. Its CALL
    // substitutes only the six-card text while keeping all native transforms.
    return true;
}

extern "C" BOOL __stdcall BJ3DrawUnifiedHand(void* board, void* graphics) noexcept {
    DrawTelemetry sample = {};
    sample.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
    sample.graphics = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(graphics));
    sample.native_return = 0;
    // The native HUD renderer can leave its local transform on Graphics.
    // Known fields are float translation (+8/+C) and scale (+10/+14),
    // confirmed by the game's rectangle submitter 0x00430FF0.
    float saved_transform[4] = {};
    float* transform = graphics ? reinterpret_cast<float*>(
        static_cast<uint8_t*>(graphics) + 8) : nullptr;
    if (transform) {
        for (uint32_t i=0;i<4;++i) saved_transform[i]=transform[i];
        transform[0]=0.0f; transform[1]=0.0f;
        transform[2]=1.0f; transform[3]=1.0f;
    }
    bj3six::HandSnapshot hand = {};
    PreviewDrawResult actual = {};
    if (board && BJ3CopyActiveHandForDisplay(sample.board,
            *reinterpret_cast<const uint32_t*>(board), &hand) &&
            DrawNativeSixHand(board, graphics, hand)) {
        actual.category=static_cast<uint32_t>(hand.evaluation.category);
        actual.faces_drawn=6;actual.label_drawn=0;
        for(unsigned i=0;i<6;++i) {actual.gem_values[i]=hand.resolved_values[i];
            actual.rectangles[i*4]=Layout(kFirstCardX+i*kCardStep,LayoutHeight());
            actual.rectangles[i*4+1]=Layout(kSixthCardY,LayoutHeight());
            actual.rectangles[i*4+2]=Layout(kCardWidth,LayoutHeight());
            actual.rectangles[i*4+3]=Layout(kCardHeight,LayoutHeight());}
        sample.fixture_state = 3;
        sample.face_drawn = 1;
        sample.generation = hand.generation;
        sample.gem_value = hand.cards[5].selected_value;
        sample.category = actual.category;
        sample.frame = static_cast<uint32_t>(sample.gem_value + 1);
        sample.x = actual.rectangles[20]; sample.y = actual.rectangles[21];
        sample.width = actual.rectangles[22]; sample.height = actual.rectangles[23];
        sample.label_drawn = actual.label_drawn;
        sample.graphics_font = actual.graphics_font;
        sample.label_x = g_BJ3PresentationTitleLeft;
        sample.label_y = Layout(kCategoryLabelY, LayoutHeight());
        g_BJ3RealHandLayoutHeight = LayoutHeight();
        for(uint32_t i=0;i<24;++i) g_BJ3RealHandRectangles[i]=actual.rectangles[i];
        for (uint32_t i=0;i<4;++i) g_BJ3RealHandPriorTransform[i]=saved_transform[i];
        g_BJ3RealHandFrontCount=actual.faces_drawn;
        InterlockedIncrement(&g_BJ3RealHandDrawCount);
        g_BJ3RealHandLastGeneration = hand.generation;
        g_BJ3RealHandLastCategory = actual.category;
        for (uint32_t i=0;i<6;++i) g_BJ3RealHandLastValues[i] = actual.gem_values[i];
        g_BJ3RealHandLastFaces = actual.faces_drawn;
    }

    if (transform) {
        for (uint32_t i=0;i<4;++i) transform[i]=saved_transform[i];
    }
    InterlockedIncrement(&g_BJ3SixthFaceDrawLastCall.sequence);
    g_BJ3SixthFaceDrawLastCall.board = sample.board;
    g_BJ3SixthFaceDrawLastCall.graphics = sample.graphics;
    g_BJ3SixthFaceDrawLastCall.native_return = sample.native_return;
    g_BJ3SixthFaceDrawLastCall.fixture_state = sample.fixture_state;
    g_BJ3SixthFaceDrawLastCall.face_drawn = sample.face_drawn;
    g_BJ3SixthFaceDrawLastCall.frame = sample.frame;
    g_BJ3SixthFaceDrawLastCall.x = sample.x;
    g_BJ3SixthFaceDrawLastCall.y = sample.y;
    g_BJ3SixthFaceDrawLastCall.width = sample.width;
    g_BJ3SixthFaceDrawLastCall.height = sample.height;
    g_BJ3SixthFaceDrawLastCall.generation = sample.generation;
    g_BJ3SixthFaceDrawLastCall.gem_value = sample.gem_value;
    g_BJ3SixthFaceDrawLastCall.category = sample.category;
    g_BJ3SixthFaceDrawLastCall.label_drawn = sample.label_drawn;
    g_BJ3SixthFaceDrawLastCall.graphics_font = sample.graphics_font;
    g_BJ3SixthFaceDrawLastCall.label_x = sample.label_x;
    g_BJ3SixthFaceDrawLastCall.label_y = sample.label_y;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixthFaceDrawLastCall.sequence);
    return sample.face_drawn != 0;
}


extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoardACOverlayDrawHook(
        void* board, void*, void* graphics, void* extra_argument) noexcept {
    typedef uint32_t (__thiscall *Fn)(void*,void*,void*);
    return reinterpret_cast<Fn>(0x006C4A00u)(board,graphics,extra_argument);
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3UnifiedHandReplacements = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixSlotCollectorDraws=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3NativeTitleSkips = 0;
extern "C" void __stdcall BJ3UnifiedHandRoute(void* board,void* graphics) noexcept {
    if(BJ3DrawUnifiedHand(board,graphics)) {
        InterlockedIncrement(&g_BJ3UnifiedHandReplacements);
        return;
    }
    typedef void (__stdcall *Fn)(void*,void*);
    if(board && graphics && *reinterpret_cast<uint32_t*>(board)==0x0081181Cu && InitNativeSixRenderer()) {
        // Preserve native acquired-card curves and effects for rows zero-four.
        memcpy(g_render_rows,static_cast<uint8_t*>(board)+0x3238,5*0xF8);
        const uint32_t count=*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(board)+0x3710);
        memcpy(g_render_rows+5*0xF8,g_render_rows+(count<5?4:0)*0xF8,0xF8);
        int32_t* sixth=reinterpret_cast<int32_t*>(g_render_rows+5*0xF8);
        sixth[0]=-2;sixth[1]=-2;
        // Follow the last native slot's initial-deal visibility. A collected
        // template must never flip an uncollected sixth card open.
        sixth[2]=*reinterpret_cast<int32_t*>(g_render_rows+4*0xF8+8);
        if(sixth[2]>=0) sixth[2]=0;
        if(!g_sixth_closed_initialized) {
            typedef void (__thiscall *Construct)(void*);
            reinterpret_cast<Construct>(0x00434240u)(g_sixth_closed_curve);
            g_sixth_closed_initialized=true;
        }
        memcpy(g_render_rows+5*0xF8+0x10,g_sixth_closed_curve,0x70);
        *reinterpret_cast<void**>(g_render_rows+5*0xF8+0xF0)=nullptr;
        reinterpret_cast<Fn>(g_native_six_renderer)(board,graphics);
        ++g_BJ3SixSlotCollectorDraws;return;
    }
    reinterpret_cast<Fn>(0x006BBE90u)(board,graphics);
}
extern "C" uint32_t __stdcall BJ3HasUnifiedHand() noexcept {
    const uint32_t app=*reinterpret_cast<const uint32_t*>(0x00887F34u);
    const uint32_t board=app ? *reinterpret_cast<const uint32_t*>(app+0xBE0u) : 0;
    bj3six::HandSnapshot hand={};
    return board && BJ3CopyActiveHandForDisplay(board,*reinterpret_cast<const uint32_t*>(board),&hand) ? 1u : 0u;
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3AnimatedTitleSubstitutions=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3AnimatedTitleLastAlpha=0;
extern "C" __declspec(dllexport) float g_BJ3AnimatedTitleLastTransform[4]={};
extern "C" __declspec(dllexport) float g_BJ3AnimatedTitleScaleRange[4]={};
extern "C" __declspec(dllexport) volatile LONG g_BJ3AnimatedTitleAlphaRange[2]={255,0};
static uint16_t g_animated_title_text[32]={};
static TargetSexyStringView g_animated_title_view={};
extern "C" void __stdcall BJ3PrepareAnimatedTitle(void* graphics,uint32_t* args) noexcept {
    const uint32_t app=*reinterpret_cast<const uint32_t*>(kAppGlobal);
    const uint32_t board=app ? *reinterpret_cast<const uint32_t*>(app+0xBE0u):0;
    bj3six::HandSnapshot hand={};
    if(!graphics || !args || !board ||
        !BJ3CopyActiveHandForDisplay(board,*reinterpret_cast<const uint32_t*>(board),&hand)) return;
    // Native Poker award atlas omits lowercase y; retain its complete uppercase glyphs.
    const char* name=hand.evaluation.category==bj3six::Category::Pyramid ? "PYRAMID" : bj3six::CategoryName(hand.evaluation.category);
    memset(&g_animated_title_view,0,sizeof(g_animated_title_view));
    memset(g_animated_title_text,0,sizeof(g_animated_title_text));
    uint32_t length=0;
    while(length<31 && name[length]) {
        g_animated_title_text[length]=static_cast<uint8_t>(name[length]);++length;
    }
    g_animated_title_view.length=length;
    if(length<8) {
        memcpy(g_animated_title_view.storage.inline_characters,g_animated_title_text,length*2);
        g_animated_title_view.capacity=7;
    } else {
        g_animated_title_view.storage.heap_data=g_animated_title_text;
        g_animated_title_view.capacity=length;
    }
    // Only argument0 changes. Native x/y/alignment, live Graphics scale/color,
    // nine-argument cleanup and the native helper's text measurement survive.
    args[0]=reinterpret_cast<uint32_t>(&g_animated_title_view);
    const uint8_t* gb=static_cast<const uint8_t*>(graphics);
    memcpy(g_BJ3AnimatedTitleLastTransform,gb+8,16);
    g_BJ3AnimatedTitleLastAlpha=*reinterpret_cast<const LONG*>(gb+0x64);
    const float sx=g_BJ3AnimatedTitleLastTransform[2],sy=g_BJ3AnimatedTitleLastTransform[3];
    if(g_BJ3AnimatedTitleSubstitutions==0) {
        g_BJ3AnimatedTitleScaleRange[0]=sx;g_BJ3AnimatedTitleScaleRange[1]=sx;
        g_BJ3AnimatedTitleScaleRange[2]=sy;g_BJ3AnimatedTitleScaleRange[3]=sy;
    } else {
        if(sx<g_BJ3AnimatedTitleScaleRange[0]) g_BJ3AnimatedTitleScaleRange[0]=sx;
        if(sx>g_BJ3AnimatedTitleScaleRange[1]) g_BJ3AnimatedTitleScaleRange[1]=sx;
        if(sy<g_BJ3AnimatedTitleScaleRange[2]) g_BJ3AnimatedTitleScaleRange[2]=sy;
        if(sy>g_BJ3AnimatedTitleScaleRange[3]) g_BJ3AnimatedTitleScaleRange[3]=sy;
    }
    if(g_BJ3AnimatedTitleLastAlpha<g_BJ3AnimatedTitleAlphaRange[0]) g_BJ3AnimatedTitleAlphaRange[0]=g_BJ3AnimatedTitleLastAlpha;
    if(g_BJ3AnimatedTitleLastAlpha>g_BJ3AnimatedTitleAlphaRange[1]) g_BJ3AnimatedTitleAlphaRange[1]=g_BJ3AnimatedTitleLastAlpha;
    ++g_BJ3AnimatedTitleSubstitutions;
}
static uintptr_t g_native_title_draw=0x004328C0u;
extern "C" __declspec(dllexport) volatile LONG g_BJ3NeedHintCategory=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SkullClearTargets=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SkullClearMask=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SkullClearXY[2]={};
static bool NativeCurveActive(void* curve) noexcept;
static double g_buster_flight_fraction=0;
static uintptr_t g_native_curve_value=0x00435510u;
static uintptr_t g_native_scaled_image=0x00431910u;
extern "C" __declspec(dllexport) volatile LONG g_BJ3BusterFlightAdjustments=0;
extern "C" __declspec(dllexport) __declspec(naked) void BJ3BusterFractionGate() noexcept {
    __asm {
        call dword ptr [g_native_curve_value]
        fst qword ptr [g_buster_flight_fraction]
        ret
    }
}
extern "C" void __stdcall BJ3AdjustBusterFlight(uint32_t* args) noexcept {
    const uint32_t app=*reinterpret_cast<uint32_t*>(kAppGlobal);
    const uint32_t board=app ? *reinterpret_cast<uint32_t*>(app+0xBE0):0;
    if(!args || !board || *reinterpret_cast<uint32_t*>(board)!=0x0081181Cu ||
        !NativeCurveActive(reinterpret_cast<void*>(board+0x3878)))return;
    const int32_t tier=*reinterpret_cast<int32_t*>(board+0x397C);
    const auto target=bj3six::MakeSkullClearTarget(tier);
    if(target.primary==bj3six::Category::Invalid)return;
    int32_t rect[4]={};void* target_rectangle=rect;
    __asm {
        mov eax,tier
        mov ecx,board
        mov edi,target_rectangle
        mov edx,006B0530h
        call edx
    }
    double fraction=g_buster_flight_fraction;
    if(fraction<0)fraction=0;if(fraction>1)fraction=1;
    const double scale=static_cast<double>(LayoutHeight())/1200.0;
    float x=0,y=0;memcpy(&x,args+2,4);memcpy(&y,args+3,4);
    // Translate the original motion progressively. At zero the meter origin
    // is untouched; at one the old skull endpoint lands on the new icon.
    x+=static_cast<float>((target.x1200-(rect[0]+rect[2]-35))*scale*fraction);
    y+=static_cast<float>((target.y1200-(rect[1]+rect[3]/2))*scale*fraction);
    memcpy(args+2,&x,4);memcpy(args+3,&y,4);
    ++g_BJ3BusterFlightAdjustments;
}
extern "C" __declspec(dllexport) __declspec(naked) void BJ3BusterFlightGate() noexcept {
    __asm {
        pushfd
        pushad
        lea eax,[esp+028h]
        push eax
        call BJ3AdjustBusterFlight
        popad
        popfd
        jmp dword ptr [g_native_scaled_image]
    }
}
extern "C" void __stdcall BJ3FillSkullClearTarget(void* board,int32_t* rectangle,int32_t tier) noexcept {
    const auto target=bj3six::MakeSkullClearTarget(tier);
    if(!board || !rectangle || target.primary==bj3six::Category::Invalid)return;
    // Native continuation computes X=rect.x+rect.width-35, Y=rect.y+height/2.
    rectangle[0]=target.x1200;rectangle[1]=target.y1200;
    rectangle[2]=35;rectangle[3]=0;
    g_BJ3SkullClearMask=target.affected_mask;
    g_BJ3SkullClearXY[0]=target.x1200;g_BJ3SkullClearXY[1]=target.y1200;
    ++g_BJ3SkullClearTargets;
}
static uintptr_t g_original_skull_rect=0x006B0530u;
extern "C" __declspec(dllexport) __declspec(naked) void BJ3SkullClearGeometryGate() noexcept {
    __asm {
        pushfd
        cmp g_BJ3PokerContext,1
        jne native_rect
        pushad
        push eax
        push edi
        push ecx
        call BJ3FillSkullClearTarget
        popad
        popfd
        mov eax,edi
        ret
    native_rect:
        popfd
        jmp dword ptr [g_original_skull_rect]
    }
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3NeedHintSubstitutions=0;
extern "C" void __stdcall BJ3FillSkullHintTarget(int32_t* rectangle,int32_t tier) noexcept {
    const auto target=bj3six::MakeSkullClearTarget(tier);
    if(!rectangle || target.primary==bj3six::Category::Invalid)return;
    // The native continuation at 6C4B21..86 uses x+167 and y+16.
    rectangle[0]=target.x1200-167;rectangle[1]=target.y1200-16;
    rectangle[2]=35;rectangle[3]=0;
}
extern "C" __declspec(naked) void BJ3SkullHintGeometryGate() noexcept {
    __asm {
        pushfd
        cmp g_BJ3PokerContext,1
        jne native_hint_rect
        pushad
        push eax
        push edi
        call BJ3FillSkullHintTarget
        popad
        popfd
        mov eax,edi
        ret
    native_hint_rect:
        popfd
        jmp dword ptr [g_original_skull_rect]
    }
}
extern "C" void __stdcall BJ3PrepareNeedHint(uint32_t* args) noexcept {
    const uint32_t app=*reinterpret_cast<uint32_t*>(kAppGlobal);
    const uint32_t board=app ? *reinterpret_cast<uint32_t*>(app+0xBE0):0;
    if(!args || !board || *reinterpret_cast<uint32_t*>(board)!=0x0081181Cu)return;
    const int32_t threshold=*reinterpret_cast<int32_t*>(board+0x397C);
    uint32_t id=11;
    while(id && bj3six::IsSkullMarked(static_cast<bj3six::Category>(id),threshold))--id;
    char text[96]={};
    if(id)sprintf_s(text,"Need %s or better",bj3six::CategoryName(static_cast<bj3six::Category>(id)));
    else strcpy_s(text,"All hands are marked");
    static uint16_t wide[96]={};static TargetSexyStringView view={};
    memset(&view,0,sizeof(view));uint32_t n=0;
    while(text[n]){wide[n]=static_cast<unsigned char>(text[n]);++n;}wide[n]=0;
    view.length=n;view.capacity=n;view.storage.heap_data=wide;
    args[0]=reinterpret_cast<uint32_t>(&view);
    g_BJ3NeedHintCategory=id;++g_BJ3NeedHintSubstitutions;
}
extern "C" __declspec(dllexport) __declspec(naked) void BJ3NativeNeedHintGate() noexcept {
    __asm {
        pushfd
        pushad
        lea eax,[esp+028h]
        push eax
        call BJ3PrepareNeedHint
        popad
        popfd
        jmp dword ptr [g_native_title_draw]
    }
}
extern "C" __declspec(naked) void BJ3NativeTitleGate() noexcept {
    __asm {
        pushfd
        pushad
        lea eax,[esp+028h]
        mov edx,[esp+018h]
        push eax
        push edx
        call BJ3PrepareAnimatedTitle
        popad
        popfd
        jmp dword ptr [g_native_title_draw]
    }
}

extern "C" __declspec(dllexport) volatile LONG g_BJ3BonusAnchorAdjustments = 0;
extern "C" void __stdcall BJ3AlignBonusAnchor(int32_t* args) noexcept {
    const uint32_t app=*reinterpret_cast<const uint32_t*>(kAppGlobal);
    const uint32_t board=app ? *reinterpret_cast<const uint32_t*>(app+0xBE0) : 0;
    bj3six::HandSnapshot hand={};
    if(!board || !BJ3CopyActiveHandForDisplay(board,*reinterpret_cast<const uint32_t*>(board),&hand)) return;
    const int32_t index=*reinterpret_cast<const int32_t*>(board+0x3788);
    if(index<0 || index>=5) return;
    const int32_t height=LayoutHeight();
    args[0]=Layout(kFirstCardX+index*kCardStep+kCardWidth/2,height);
    args[1]=Layout(kSixthCardY+kCardHeight/2,height);
    InterlockedIncrement(&g_BJ3BonusAnchorAdjustments);
}
static uintptr_t g_native_bonus=0x006F2A70u;
extern "C" __declspec(naked) void BJ3BonusAnchorGate() noexcept {
    __asm {
        pushfd
        pushad
        lea eax,[esp+028h]
        push eax
        call BJ3AlignBonusAnchor
        popad
        popfd
        jmp dword ptr [g_native_bonus]
    }
}

static bool InstallUnifiedCall(uintptr_t address,uintptr_t original,uintptr_t replacement) noexcept {
    uint8_t* site=reinterpret_cast<uint8_t*>(address);
    uint8_t expected[5]={0xE8}, patch[5]={0xE8};
    const int32_t rel_original=static_cast<int32_t>(original-address-5);
    const int32_t rel_patch=static_cast<int32_t>(replacement-address-5);
    memcpy(expected+1,&rel_original,4);memcpy(patch+1,&rel_patch,4);
    if(memcmp(site,patch,5)==0) return true;
    if(memcmp(site,expected,5)!=0) return false;
    DWORD old=0,ignored=0;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old)) return false;
    memcpy(site,patch,5);
    const BOOL flushed=FlushInstructionCache(GetCurrentProcess(),site,5);
    const BOOL restored=VirtualProtect(site,5,old,&ignored);
    return flushed && restored && memcmp(site,patch,5)==0;
}


extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenHudDraws=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenHudRows=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenHudThreshold=-1;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenHudSkullMask=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenHudActiveCategory=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenHudLayoutHeight=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenHudRowRects[44]={};
static bool NativeCurveActive(void* curve) noexcept {
    uint32_t result=0;
    __asm {
        mov esi,curve
        mov eax,004357E0h
        call eax
        movzx eax,al
        mov result,eax
    }
    return result!=0;
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3SkullEntryDraws=0;
static bool DrawElevenPayoutHud(void* board,void* graphics) noexcept {
    if(!board || !graphics || *reinterpret_cast<uint32_t*>(board)!=0x0081181Cu) return false;
    const uint32_t app=*reinterpret_cast<const uint32_t*>(kAppGlobal);
    const uint32_t address=reinterpret_cast<uint32_t>(board);
    if(!app || *reinterpret_cast<const uint32_t*>(app+0xBE0u)!=address) return false;
    void* front=*reinterpret_cast<void**>(kCardsFrontGlobal);
    void* skull=*reinterpret_cast<void**>(0x0087F78Cu);
    const uint32_t small_font=*reinterpret_cast<const uint32_t*>(0x0087EA20u);
    if(!front || !small_font) return false;
    uint8_t* gb=static_cast<uint8_t*>(graphics);
    float transform[4]={};memcpy(transform,gb+8,16);
    const PresentationColor saved_color=*reinterpret_cast<const PresentationColor*>(gb+0x58);
    const uint8_t colorize=gb[0x70];
    const uint32_t saved_font=*reinterpret_cast<const uint32_t*>(gb+0x68);
    float* tr=reinterpret_cast<float*>(gb+8);tr[0]=0;tr[1]=0;tr[2]=1;tr[3]=1;
    gb[0x70]=1;*reinterpret_cast<uint32_t*>(gb+0x68)=small_font;
    const int32_t height=LayoutHeight();
    // Replace only the old table rectangle. Keep the native panel frame,
    // score/meter, deck, buttons and six-card settlement outside this region.
    // The derived native HUD omits the old table. No cover rectangle is drawn.
    const int32_t threshold=bj3six::IsQuestPoker()?-1:*reinterpret_cast<const int32_t*>(address+0x397Cu);
    const uint32_t mask=bj3six::SkullMaskForThreshold(threshold);
    bj3six::HandSnapshot hand={};
    uint32_t active=0;
    if(BJ3CopyActiveHandForDisplay(address,*reinterpret_cast<const uint32_t*>(board),&hand)) {
        active=static_cast<uint32_t>(hand.evaluation.category);
    } else {
        const uint32_t count=*reinterpret_cast<const uint32_t*>(address+0x3710u);
        uint8_t masks[5]={};
        if(count<=5) {
            for(uint32_t i=0;i<count;++i) {
                const int32_t* card=reinterpret_cast<const int32_t*>(address+0x3238u+i*0xF8u);
                if(card[2]==1) masks[i]=127;
                else {
                    if(card[0]>=0 && card[0]<7) masks[i]|=static_cast<uint8_t>(1u<<card[0]);
                    if(card[1]>=0 && card[1]<7) masks[i]|=static_cast<uint8_t>(1u<<card[1]);
                }
            }
            static uint32_t previous_count=~0u,previous_category=0;
            static uint8_t previous_masks[5]={};
            if(previous_count!=count || memcmp(previous_masks,masks,sizeof(masks))!=0) {
                previous_category=static_cast<uint32_t>(bj3six::EvaluatePresentOptions(masks,count));
                previous_count=count;memcpy(previous_masks,masks,sizeof(masks));
            }
            active=previous_category;
        }
    }
    uint32_t rows=0;
    for(uint32_t id=1;id<=11;++id) {
        const auto model=bj3six::MakeTableRow(id-1,height,threshold,static_cast<bj3six::Category>(active));
        const bj3six::Category category=model.category;
        const int32_t baseline=bj3six::TableTop()+11+static_cast<int32_t>(id-1)*13;
        FaceDestination row={model.bounds.x,model.bounds.y,model.bounds.width,model.bounds.height};
        g_BJ3ElevenHudRowRects[(id-1)*4]=row.x;
        g_BJ3ElevenHudRowRects[(id-1)*4+1]=row.y;
        g_BJ3ElevenHudRowRects[(id-1)*4+2]=row.width;
        g_BJ3ElevenHudRowRects[(id-1)*4+3]=row.height;
        const bool marked=model.dangerous;
        if(marked) {
            SetPresentationColor(graphics,PresentationColor{126,30,20,255});
            DrawAtlasCard(front,graphics,-1,&row);
        }
        SetPresentationColor(graphics,PresentationColor{255,255,228,255});
        uint32_t font=0;char score[16]={};sprintf_s(score,"%d",model.score);
        const bool named=DrawStringAt(graphics,bj3six::CategoryName(category),Layout(113,height),Layout(baseline,height),&font);
        const bool scored=DrawStringAt(graphics,score,Layout(216,height),Layout(baseline,height),&font,false,true);
        if(named && scored) ++rows;

    }
    // Draw luminous table rules before the native moving cards.
    for(int pass=0;pass<2;++pass) {
        SetPresentationColor(graphics,pass==0 ? PresentationColor{65,92,35,45}:PresentationColor{116,145,72,160});
        const int thickness=pass==0 ? 3:1;
        for(int line=0;line<=11;++line) {
            const int y=bj3six::TableTop()+line*13;
            FaceDestination edge={Layout(101,height),Layout(y-(pass==0?1:0),height),Layout(118,height),Layout(thickness,height)};
            DrawAtlasCard(front,graphics,-1,&edge);
        }
        for(int x: {101,176,219}) {
            FaceDestination edge={Layout(x-(pass==0?1:0),height),Layout(bj3six::TableTop(),height),Layout(thickness,height),Layout(143,height)};
            DrawAtlasCard(front,graphics,-1,&edge);
        }
    }
    if(active>=1 && active<=11) {
        const auto focus=bj3six::MakeTableRow(active-1,height,threshold,static_cast<bj3six::Category>(active));
        const auto b=focus.bounds;
        for(int pass=0;pass<2;++pass) {
            const int32_t margin=pass==0 ? Layout(1,height):0;
            const int32_t line=Layout(pass==0 ? 4:1,height);
            const int32_t x=b.x-margin,y=b.y-margin,w=b.width+2*margin,h=b.height+2*margin;
            SetPresentationColor(graphics,PresentationColor{255,255,255,pass==0 ? 64:255});
            FaceDestination edges[4]={{x,y,w,line},{x,y+h-line,w,line},
                {x,y,line,h},{x+w-line,y,line,h}};
            for(auto& edge:edges)DrawAtlasCard(front,graphics,-1,&edge);
        }
    }
    // Last table pass: static and animated skulls must cover row/grid glow.
    for(uint32_t id=1;id<=11;++id) {
        const auto category=static_cast<bj3six::Category>(id);
        const int32_t baseline=bj3six::TableTop()+11+static_cast<int32_t>(id-1)*13;
        const bool marked=(mask&(1u<<(id-1)))!=0;
        uint32_t font=0;
        if(marked) {
            if(skull && *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(skull)+0x34)>0 &&
                    *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(skull)+0x38)>0) {
                FaceDestination icon={Layout(102,height),Layout(baseline-10,height),Layout(10,height),Layout(10,height)};
                void* image=skull;int32_t alpha=255;
                void* curve=reinterpret_cast<void*>(address+0x3800);
                if(bj3six::DangerIndex(category)==threshold && NativeCurveActive(curve)) {
                    typedef double (__thiscall *Value)(void*);
                    const double value=reinterpret_cast<Value>(0x00435510u)(curve);
                    // Same native curve-driven scale/alpha as 0x006BEA09..B0B.
                    const double scale=value*2.0+1.0;
                    double fade=value*5.0-4.0;if(fade<0)fade=0;
                    alpha=static_cast<int32_t>((1.0-fade)*255.0);
                    if(alpha<0)alpha=0;if(alpha>255)alpha=255;
                    if(scale>0 && scale<8) {
                        const int32_t w=static_cast<int32_t>(icon.width*scale),h=static_cast<int32_t>(icon.height*scale);
                        icon.x-=(w-icon.width)/2;icon.y-=(h-icon.height)/2;
                        icon.width=w;icon.height=h;
                    }
                    void* animated=*reinterpret_cast<void**>(0x0087E4BCu);
                    if(animated)image=animated;
                    ++g_BJ3SkullEntryDraws;
                }
                SetPresentationColor(graphics,PresentationColor{255,255,255,alpha});
                DrawAtlasCard(image,graphics,-1,&icon);
            } else DrawStringAt(graphics,"!",Layout(103,height),Layout(baseline,height),&font);
        }
    }
    g_BJ3ElevenHudRows=rows;g_BJ3ElevenHudThreshold=threshold;
    g_BJ3ElevenHudSkullMask=mask;g_BJ3ElevenHudActiveCategory=active;g_BJ3ElevenHudLayoutHeight=height;
    ++g_BJ3ElevenHudDraws;
    SetPresentationColor(graphics,saved_color);gb[0x70]=colorize;
    *reinterpret_cast<uint32_t*>(gb+0x68)=saved_font;memcpy(gb+8,transform,16);
    return rows==11;
}
#include "native_hud_bytes.inc"
static void* g_native_hud_without_table=nullptr;
#include "native_hover_bytes.inc"
static void* g_native_hover_without_table=nullptr;
static bool InitNativeHoverWithoutTable() noexcept {
    if(g_native_hover_without_table)return true;
    if(memcmp(reinterpret_cast<void*>(0x006B4550u),kNativeHoverBytes,sizeof(kNativeHoverBytes)))return false;
    auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,sizeof(kNativeHoverBytes),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code)return false;
    memcpy(code,kNativeHoverBytes,sizeof(kNativeHoverBytes));
    for(unsigned offset:kNativeHoverRel32){int32_t rel=0;memcpy(&rel,code+offset,4);rel+=static_cast<int32_t>(0x006B4550u-reinterpret_cast<uint32_t>(code));memcpy(code+offset,&rel,4);}
    const unsigned offset=0x006B4615u-0x006B4550u;
    code[offset]=0x31;code[offset+1]=0xFF;code[offset+2]=0xE9;
    const int32_t jump=static_cast<int32_t>(0x006B4AE5u-(0x006B4615u+7));
    memcpy(code+offset+3,&jump,4);
    DWORD old=0;
    if(!VirtualProtect(code,sizeof(kNativeHoverBytes),PAGE_EXECUTE_READ,&old)){VirtualFree(code,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),code,sizeof(kNativeHoverBytes));
    g_native_hover_without_table=code;return true;
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3TableHoverCategory=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3TableHoverCalls=0;
static TargetSexyStringView NativeOwnedText(const char* ascii) noexcept {
    wchar_t wide[512]={};uint32_t n=0;
    while(ascii[n] && n<511){wide[n]=static_cast<unsigned char>(ascii[n]);++n;}
    TargetSexyStringView text={};text.capacity=7;
    typedef void (__thiscall *Assign)(void*,const wchar_t*,uint32_t);
    reinterpret_cast<Assign>(0x00401A70u)(&text,wide,n);
    return text;
}
extern "C" __declspec(dllexport) void __fastcall BJ3PokerTableHoverHook(void* board,void*) noexcept {
    typedef void (__thiscall *Original)(void*);
    reinterpret_cast<Original>(g_native_hover_without_table)(board);
    g_BJ3TableHoverCategory=0;
    const uint32_t address=reinterpret_cast<uint32_t>(board);
    typedef bool (__thiscall *CanHover)(void*);
    const uint32_t vptr=*reinterpret_cast<uint32_t*>(board);
    if(!reinterpret_cast<CanHover>(*reinterpret_cast<uint32_t*>(vptr+0x17C))(board))return;
    const uint32_t manager=*reinterpret_cast<uint32_t*>(address+0x20);
    const uint32_t app=*reinterpret_cast<uint32_t*>(kAppGlobal);
    if(!manager || !app)return;
    const int32_t height=LayoutHeight();
    const auto category=bj3six::HitTable(*reinterpret_cast<int32_t*>(manager+0x10C),*reinterpret_cast<int32_t*>(manager+0x110),height);
    if(category==bj3six::Category::Invalid)return;
    const auto row=bj3six::MakeTableRow(static_cast<unsigned>(category)-1,height,*reinterpret_cast<int32_t*>(address+0x397C),bj3six::Category::Invalid);
    char description[512]={};sprintf_s(description,"%s%s",row.description,row.dangerous ? " This hand is marked with a skull.":"");
    auto title=NativeOwnedText(row.name);auto body=NativeOwnedText(description);
    struct Point {int32_t x,y;};
    typedef void (__fastcall *Tooltip)(uint32_t,uint32_t,void*,void*,TargetSexyStringView,TargetSexyStringView,int32_t,int32_t,int32_t,int32_t,int32_t,Point);
    // Native callee consumes its two owned by-value string arguments (ret 0x5C).
    reinterpret_cast<Tooltip>(0x00669590u)(0,0,*reinterpret_cast<void**>(app+0xBE8),board,title,body,height*320/1200,2,0,0,-1,Point{row.bounds.x+row.bounds.width,row.bounds.y+row.bounds.height/2});
    g_BJ3TableHoverCategory=static_cast<LONG>(category);++g_BJ3TableHoverCalls;
}
static bool InitNativeHudWithoutTable() noexcept {
    if(g_native_hud_without_table) return true;
    if(memcmp(reinterpret_cast<void*>(0x006BE510u),kNativeHudBytes,sizeof(kNativeHudBytes)))return false;
    auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,sizeof(kNativeHudBytes),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code)return false;
    memcpy(code,kNativeHudBytes,sizeof(kNativeHudBytes));
    for(unsigned offset:kNativeHudRel32){
        int32_t rel=0;memcpy(&rel,code+offset,4);
        rel+=static_cast<int32_t>(0x006BE510u-reinterpret_cast<uint32_t>(code));
        memcpy(code+offset,&rel,4);
    }
    // Skip only seven-row table fill/text/skulls/highlight, before any of its
    // stack temporaries or strings are constructed. Restore the tail's Board
    // and Graphics registers exactly as the old table tail did.
    const unsigned offset=0x006BE70Fu-0x006BE510u;
    // Call the replacement at the old table layer, before native cards/effects.
    // After PUSHFD/PUSHAD and PUSH graphics, Board [original ESP+1C] is +44.
    unsigned char route[]={0x9C,0x60,0xFF,0x75,0x08,0xFF,0x74,0x24,0x44,
        0xB8,0,0,0,0,0xFF,0xD0,0x83,0xC4,0x08,0x61,0x9D,
        0x8B,0x5C,0x24,0x1C,0x8B,0x75,0x08,0xE9,0,0,0,0};
    const uint32_t draw=reinterpret_cast<uint32_t>(&DrawElevenPayoutHud);
    memcpy(route+10,&draw,4);
    const int32_t jump=static_cast<int32_t>(0x006BECFEu-(0x006BE70Fu+sizeof(route)));
    memcpy(route+sizeof(route)-4,&jump,4);memcpy(code+offset,route,sizeof(route));
    // This copied call no longer sees the original site's installed redirect.
    int32_t hand=static_cast<int32_t>(reinterpret_cast<uint32_t>(&BJ3UnifiedHandRoute)-
        (reinterpret_cast<uint32_t>(code)+(0x006BF7A4u-0x006BE510u)+5));
    memcpy(code+(0x006BF7A4u-0x006BE510u)+1,&hand,4);
    DWORD old=0;
    if(!VirtualProtect(code,sizeof(kNativeHudBytes),PAGE_EXECUTE_READ,&old)){VirtualFree(code,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),code,sizeof(kNativeHudBytes));
    g_native_hud_without_table=code;return true;
}
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerElevenHudHook(void* board,void*,void* graphics) noexcept {
    typedef uint32_t (__thiscall *Original)(void*,void*);
    const uint32_t result=reinterpret_cast<Original>(g_native_hud_without_table)(board,graphics);
    return result;
}
static bool SetHudSlot(bool install) noexcept {
    volatile LONG* slot=reinterpret_cast<volatile LONG*>(0x00811C00u);
    const LONG hook=reinterpret_cast<LONG>(&BJ3PokerElevenHudHook);
    const LONG expected=install ? 0x006BE510:hook;
    const LONG replacement=install ? hook:0x006BE510;
    if(*slot==replacement) return true;
    if(*slot!=expected) return false;
    DWORD old=0,ignored=0;
    if(!VirtualProtect(const_cast<LONG*>(slot),4,PAGE_READWRITE,&old)) return false;
    const LONG previous=InterlockedCompareExchange(slot,replacement,expected);
    const BOOL restored=VirtualProtect(const_cast<LONG*>(slot),4,old,&ignored);
    return restored && previous==expected && *slot==replacement;
}
static bool SetTableHoverSlot(bool install) noexcept {
    volatile LONG* slot=reinterpret_cast<volatile LONG*>(0x00811BACu);
    const LONG hook=reinterpret_cast<LONG>(&BJ3PokerTableHoverHook);
    const LONG expected=install ? 0x006B4550:hook,replacement=install ? hook:0x006B4550;
    if(*slot==replacement)return true;
    if(*slot!=expected)return false;
    DWORD old=0,ignored=0;if(!VirtualProtect(const_cast<LONG*>(slot),4,PAGE_READWRITE,&old))return false;
    const LONG previous=InterlockedCompareExchange(slot,replacement,expected);
    const BOOL restored=VirtualProtect(const_cast<LONG*>(slot),4,old,&ignored);
    return restored && previous==expected;
}
static bool SetCollectorFlightStep(bool install) noexcept {
    // Both the virtual destination helper and actual appender FX destination.
    for(uint32_t address: {0x006B0622u,0x006B967Au}) {
        volatile LONG* operand=reinterpret_cast<volatile LONG*>(address);
        const LONG replacement=install ? reinterpret_cast<LONG>(&g_collector_card_step):0x00828860;
        const LONG expected=install ? 0x00828860:reinterpret_cast<LONG>(&g_collector_card_step);
        if(*operand==replacement)continue;
        if(*operand!=expected)return false;
        DWORD old=0,ignored=0;
        if(!VirtualProtect(const_cast<LONG*>(operand),4,PAGE_EXECUTE_READWRITE,&old))return false;
        memcpy(const_cast<LONG*>(operand),&replacement,4);
        const BOOL restored=VirtualProtect(const_cast<LONG*>(operand),4,old,&ignored);
        FlushInstructionCache(GetCurrentProcess(),const_cast<LONG*>(operand),4);
        if(!restored)return false;
    }
    return true;
}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3InstallPokerBoardFaceDrawHook(LPVOID unused) noexcept {
    (void)unused;
    if (!InitNativeSixRenderer()) return 18;
    if (!InitNativeHudWithoutTable()) return 20;
    if (!InitNativeHoverWithoutTable()) return 21;
    if (!SetTableHoverSlot(true)) return 22;
    if (!SetHudSlot(true)) return 19;
    if (!InstallUnifiedCall(0x006B8123u,0x006B0530u,reinterpret_cast<uintptr_t>(&BJ3SkullClearGeometryGate))) return 25;
    if (!InstallUnifiedCall(0x006BB974u,0x00435510u,reinterpret_cast<uintptr_t>(&BJ3BusterFractionGate)) ||
        !InstallUnifiedCall(0x006BBB9Bu,0x00431910u,reinterpret_cast<uintptr_t>(&BJ3BusterFlightGate)) ||
        !InstallUnifiedCall(0x006BBC69u,0x00431910u,reinterpret_cast<uintptr_t>(&BJ3BusterFlightGate))) return 26;
    if (!SetCollectorFlightStep(true)) return 24;
    if (!InstallUnifiedCall(0x006C4AEEu,0x006B0530u,reinterpret_cast<uintptr_t>(&BJ3SkullHintGeometryGate))) return 27;
    if (!InstallUnifiedCall(0x006B4279u,0x004328C0u,reinterpret_cast<uintptr_t>(&BJ3NativeNeedHintGate))) return 23;
    if (!InstallUnifiedCall(0x006BF7A4u,0x006BBE90u,reinterpret_cast<uintptr_t>(&BJ3UnifiedHandRoute)) ||
        !InstallUnifiedCall(0x006C540Fu,0x006BBE90u,reinterpret_cast<uintptr_t>(&BJ3UnifiedHandRoute)) ||
        !InstallUnifiedCall(0x006C4FA5u,0x004328C0u,reinterpret_cast<uintptr_t>(&BJ3NativeTitleGate)) ||
        !InstallUnifiedCall(0x006C4669u,0x006F2A70u,reinterpret_cast<uintptr_t>(&BJ3BonusAnchorGate))) return 16;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(kImageBase)) return 11;
    const LONG hook = static_cast<LONG>(reinterpret_cast<uintptr_t>(
        &BJ3PokerBoardACOverlayDrawHook));
    volatile LONG* slot = reinterpret_cast<volatile LONG*>(
        static_cast<uintptr_t>(kPokerDrawSlotAddress));
    const LONG current = *slot;
    if (current == hook) return 1;
    if (current != static_cast<LONG>(kPokerDrawOriginal)) return 12;

    DWORD old_protection = 0;
    if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
            PAGE_READWRITE, &old_protection)) return 13;
    const LONG replaced = InterlockedCompareExchange(slot, hook,
        static_cast<LONG>(kPokerDrawOriginal));
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
        old_protection, &ignored);
    if (replaced != static_cast<LONG>(kPokerDrawOriginal)) return 14;
    return restored ? 1 : 15;
}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3UninstallPokerBoardFaceDrawHook(LPVOID unused) noexcept {
    (void)unused;
    if (!SetTableHoverSlot(false)) return 22;
    if (!SetCollectorFlightStep(false)) return 24;
    if (!SetHudSlot(false)) return 19;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(kImageBase)) return 11;
    const LONG hook = static_cast<LONG>(reinterpret_cast<uintptr_t>(
        &BJ3PokerBoardACOverlayDrawHook));
    volatile LONG* slot = reinterpret_cast<volatile LONG*>(
        static_cast<uintptr_t>(kPokerDrawSlotAddress));
    if (*slot == static_cast<LONG>(kPokerDrawOriginal)) return 1;
    if (*slot != hook) return 12;
    DWORD old_protection = 0;
    if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
            PAGE_READWRITE, &old_protection)) return 13;
    const LONG replaced = InterlockedCompareExchange(slot,
        static_cast<LONG>(kPokerDrawOriginal), hook);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
        old_protection, &ignored);
    if (replaced != hook) return 14;
    return restored ? 1 : 15;
}
