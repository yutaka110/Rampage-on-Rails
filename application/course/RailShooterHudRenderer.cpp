#include "RailShooterHudRenderer.h"
#include "RailShortCourse.h"
#include "RailGameOverScene.h"
#include "EncounterPerformanceScoreSystem.h"
#include <cmath>

#include <algorithm>
#include <string_view>

namespace {
std::string Utf8(std::u8string_view text) {
    return {
        reinterpret_cast<const char*>(text.data()),
        reinterpret_cast<const char*>(text.data() + text.size())};
}

Vector4 WithOpacity(Vector4 color, float opacity) {
    color.w *= (std::clamp)(opacity, 0.0f, 1.0f);
    return color;
}
} // namespace

void RailShooterHudRenderer::Reset() {
    frame_ = {};
    revision_ = 0;
}

void RailShooterHudRenderer::Update(const RailShooterHudRenderInput& input) {
    RailShooterHudRenderFrame next{};
    next.viewportWidth = input.viewportWidth;
    next.viewportHeight = input.viewportHeight;
    next.revision = ++revision_;
    const bool gameOver=input.gameOver && input.gameOver->active && !input.showTitleScreen;
    if (input.definition == nullptr || input.presentation == nullptr ||
        (!input.showTitleScreen && !gameOver &&
         (!input.definition->enabled || !input.presentation->visible)) ||
        input.viewportWidth < 320 || input.viewportHeight < 180) {
        frame_ = std::move(next);
        return;
    }

    const RailShooterHudDefinitionAsset& definition = *input.definition;
    const RailShooterHudPresentationFrame& hud = *input.presentation;
    const float width = static_cast<float>(input.viewportWidth);
    const float height = static_cast<float>(input.viewportHeight);
    const float responsive = (std::clamp)(
        (std::min)(width / 1600.0f, height / 900.0f), 0.20f, 1.35f);
    const float scale = responsive * definition.scale;
    const float safe = definition.safeAreaPixels * responsive;
    const float opacity = definition.opacity;
    const uint32_t budget = definition.maximumDrawCommands;
    next.commands.reserve((std::min)(budget, 96u));

    const auto push = [&next, budget](RailShooterHudDrawCommand command) {
        if (next.commands.size() < budget) {
            next.commands.push_back(std::move(command));
        }
    };
    const auto rect = [&push](float x, float y, float w, float h, Vector4 color) {
        if (w <= 0.0f || h <= 0.0f || color.w <= 0.0f) return;
        RailShooterHudDrawCommand command{};
        command.x = x; command.y = y; command.width = w; command.height = h;
        command.color = color;
        push(std::move(command));
    };
    const auto text = [&push](
        std::string value,
        float x,
        float y,
        float fontScale,
        Vector4 color,
        RailShooterHudTextAlignment alignment = RailShooterHudTextAlignment::Left) {
        if (value.empty() || color.w <= 0.0f) return;
        RailShooterHudDrawCommand command{};
        command.kind = RailShooterHudDrawCommandKind::Text;
        command.textAlignment = alignment;
        command.x = x; command.y = y; command.fontScale = fontScale;
        command.color = color; command.text = std::move(value);
        push(std::move(command));
    };
    const auto bar = [&rect](
        float x, float y, float w, float h, float value, Vector4 color,
        Vector4 background, float trail = -1.0f) {
        rect(x, y, w, h, background);
        if(trail>value) rect(x+w*value,y,w*((std::min)(trail,1.0f)-value),h,{1.0f,0.72f,0.32f,color.w*0.85f});
        rect(x, y, w * (std::clamp)(value, 0.0f, 1.0f), h, color);
    };

    if(gameOver && input.resultSession) {
        const auto& scene=*input.gameOver;
        const auto& result=*input.resultSession;
        const auto layout=GameOverLayout(width,height);
        const float s=layout.scale,cx=layout.menuCenterX,cy=layout.centerY;
        const auto center=RailShooterHudTextAlignment::Center;
        if(scene.wreck && scene.hudOpacity>0 && definition.enabled && hud.visible) {
            // Reuse the normal layout while the gameplay camera is held.
            // Show the fatal health value immediately, then fade the HUD with
            // the dolly instead of replacing it with a result card in one frame.
            auto faultHud=hud;
            faultHud.playerHealthNormalized=faultHud.playerHealthTrail=0;
            faultHud.playerHealthCritical=true;
            faultHud.healthText=Utf8(u8"体力 0/")+std::to_string(int(result.maximumPlayerHealth));
            faultHud.showBanner=faultHud.showDamageNotice=false;
            auto faultSession=result;
            faultSession.phase=GameSessionPhase::Playing;faultSession.outcome=GameSessionOutcome::None;
            auto faultInput=input;
            faultInput.gameOver=nullptr;faultInput.presentation=&faultHud;faultInput.resultSession=&faultSession;
            RailShooterHudRenderer faultLayout;faultLayout.Update(faultInput);
            for(auto command:faultLayout.Frame().commands) {
                command.color.w*=scene.hudOpacity;
                push(std::move(command));
            }
        }
        if(scene.impactFlash>0) {
            rect(0,0,width,height,{0.65f,0.16f,0.025f,scene.impactFlash*0.18f});
            rect(0,0,width,9*s,{1.0f,0.38f,0.08f,scene.impactFlash});
            rect(0,height-9*s,width,9*s,{1.0f,0.38f,0.08f,scene.impactFlash});
        }
        // A fully opaque black texel closes the wreck before any menu glyphs.
        rect(0,0,width,height,{0.006f,0.006f,0.008f,scene.blackout});
        const float a=scene.titleOpacity,m=scene.menuOpacity;
        if(scene.wreck && a>0) {
            RailShooterHudDrawCommand stillLife;
            stillLife.kind=RailShooterHudDrawCommandKind::GameOverDiorama;
            stillLife.x=layout.preview.x;stillLife.y=layout.preview.y;
            stillLife.width=layout.preview.width;stillLife.height=layout.preview.height;
            stillLife.color={1,1,1,a};stillLife.transitionProgress=scene.elapsed;
            push(std::move(stillLife));
        }
        const Vector4 amber{0.94f,0.72f,0.40f,a},white{1,0.96f,0.88f,m},muted{0.60f,0.57f,0.52f,m};
        text("R U N  T E R M I N A T E D",cx,cy-170*s,0.62f*s,{0.60f,0.48f,0.35f,a},center);
        RailShooterHudDrawCommand headline;
        headline.kind=RailShooterHudDrawCommandKind::Text;headline.textAlignment=center;
        headline.textStyle=RailShooterHudTextStyle::StartCue;headline.text="GAME OVER";
        headline.x=cx;headline.y=cy-88*s;headline.fontScale=3.6f*s;headline.color=amber;
        if(a>0) push(std::move(headline));
        rect(cx-32*s,cy-56*s,64*s,2*s,amber);
        text(result.endReason==GameSessionEndReason::TimeExpired?Utf8(u8"制限時間を過ぎました"):
            result.endReason==GameSessionEndReason::PlayerDestroyed?Utf8(u8"トロッコが大破しました"):Utf8(u8"任務を継続できません"),
            cx,cy-22*s,0.82f*s,{0.74f,0.68f,0.58f,a},center);
        text("SCORE  "+std::to_string(result.score)+"    /    MAX CHAIN  "+std::to_string(result.maximumCombo),
            cx,cy+20*s,0.72f*s,muted,center);
        for(int i=0;i<2;++i) {
            const auto button=GameOverButton(width,height,i);
            const bool enabled=i==1 || input.checkpointRetryAvailable;
            const bool selected=scene.selection==i && enabled;
            rect(button.x,button.y,button.width,button.height,selected?Vector4{0.17f,0.12f,0.065f,m}:Vector4{0.035f,0.03f,0.025f,m});
            if(selected) {
                rect(button.x,button.y,3*s,button.height,{0.94f,0.72f,0.40f,m});
                text(">",button.x+20*s,button.y+39*s,0.90f*s,{0.94f,0.72f,0.40f,m});
            }
            text(i==0?Utf8(u8"リトライ"):Utf8(u8"タイトルに戻る"),cx,button.y+38*s,1.05f*s,enabled?white:muted,center);
        }
        text(input.checkpointRetryAvailable?Utf8(u8"リトライ：最後のチェックポイントから再開"):
            Utf8(u8"チェックポイントからの再開は利用できません"),cx,cy+239*s,0.65f*s,muted,center);
        text(Utf8(u8"↑ / ↓・W / S：選択    ENTER：決定"),cx,cy+278*s,0.60f*s,muted,center);
        text(Utf8(u8"マウス：選択 / 決定"),cx,cy+304*s,0.60f*s,muted,center);
        next.visible=!next.commands.empty();frame_=std::move(next);return;
    }

    if(!input.showTitleScreen && input.resultSession && IsRunResult(*input.resultSession)) {
        const auto& result=*input.resultSession;
        const float s=(std::min)(width/1600.0f,height/900.0f);
        const float cx=width*0.5f,cy=height*0.5f;
        const auto center=RailShooterHudTextAlignment::Center;
        const Vector4 accent=result.outcome==GameSessionOutcome::Victory ?
            Vector4{0.35f,0.94f,0.80f,1}:Vector4{1.0f,0.47f,0.28f,1};
        const Vector4 white{1,0.98f,0.93f,1},muted{0.75f,0.79f,0.80f,1};
        // Put essential actions first in the draw budget; no combat HUD or
        // startup opacity can suppress the result screen's navigation.
        rect(0,0,width,height,{0.012f,0.018f,0.025f,0.65f});
        rect(cx-320*s,cy-250*s,640*s,500*s,{0.018f,0.026f,0.033f,0.96f});
        rect(cx-320*s,cy-250*s,640*s,3*s,accent);
        for(int i=0;i<3;++i) {
            const bool enabled=i!=1 || result.canRetry;
            const auto button=RunResultButton(width,height,i);
            rect(button.x,button.y,button.width,button.height,
                enabled?Vector4{0.075f,0.11f,0.13f,1}:Vector4{0.035f,0.045f,0.05f,1});
            const std::string labels[]={Utf8(u8"最初から再挑戦  [ ENTER / R ]"),
                result.canRetry?Utf8(u8"区間から再開  [ C ]"):Utf8(u8"区間から再開できません"),Utf8(u8"タイトルに戻る  [ T ]")};
            text(labels[i],cx,button.y+33*s,0.90f*s,enabled?white:muted,center);
        }
        text(result.outcome==GameSessionOutcome::Victory?Utf8(u8"任務完了"):Utf8(u8"任務失敗"),
            cx,cy-181*s,1.75f*s,accent,center);
        std::string reason=result.outcome==GameSessionOutcome::Victory?Utf8(u8"包囲を突破しました"):
            (result.endReason==GameSessionEndReason::TimeExpired?Utf8(u8"制限時間を過ぎました"):Utf8(u8"車体の耐久がなくなりました"));
        text(reason,cx,cy-144*s,0.78f*s,muted,center);
        text("SCORE   "+std::to_string(result.score),cx,cy-82*s,1.18f*s,white,center);
        text("MAX CHAIN   "+std::to_string(result.maximumCombo),cx,cy-43*s,0.86f*s,white,center);
        const int seconds=int((std::max)(0.0f,result.gameplayElapsedSeconds));
        text("RUN TIME   "+std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60)+
            "    HP   "+std::to_string(int(std::ceil(result.playerHealth))),cx,cy-6*s,0.80f*s,muted,center);
        next.visible=!next.commands.empty(); frame_=std::move(next); return;
    }

    if (input.showTitleScreen) {
        const float s = (std::min)(width / 1600.0f, height / 900.0f);
        const float left = (width-1600.0f*s)*0.5f;
        const float top = (height-900.0f*s)*0.5f;
        const Vector4 accent{0.94f,0.76f,0.43f,1};
        const Vector4 white{1.0f,0.98f,0.91f,1};
        const Vector4 muted{0.83f,0.81f,0.75f,1};
        // Adjacent bands form a broad, quiet scrim. Overlapping all bands at
        // the left edge made an opaque strip beside the old card-style menu.
        // Leave room for the logo, menu/help and the final transition blackout
        // even with a smaller authored HUD draw budget.
        const int scrimBands = int((std::min)(80u, budget > 24u ? budget-20u : 4u));
        for (int i=0;i<scrimBands;++i) {
            const float t = float(i)/float(scrimBands-1);
            const float bandWidth = (left+720*s)/float(scrimBands);
            const float fade = 1.0f-t*t*(3.0f-2.0f*t);
            rect(i*bandWidth,0,bandWidth,height,{0.035f,0.045f,0.065f,0.62f*fade});
        }
        const float x = left+96*s;
        if (input.titleLogoAvailable) {
            RailShooterHudDrawCommand logo{};
            logo.kind = RailShooterHudDrawCommandKind::TitleLogo;
            logo.x = left+60*s;
            logo.y = top+80*s;
            logo.width = 560*s;
            logo.height = (560.0f*2.0f/3.0f)*s;
            logo.color = {1,1,1,1};
            // A restrained screen-space shadow keeps the flat white lettering
            // readable without baking thick extrusion into every character.
            auto shadow = logo;
            shadow.x += 1.5f*s;
            shadow.y += 2.5f*s;
            shadow.color = {0.08f,0.06f,0.04f,0.22f};
            push(std::move(shadow));
            push(std::move(logo));
        } else {
            // Retain a readable title if a packaged image is missing/corrupt.
            text(Utf8(u8"レールで"),x,top+279*s,3.3f*s,white);
            text(Utf8(u8"あばレール"),x,top+370*s,3.3f*s,white);
        }
        if (!input.titleControlsVisible) {
            const std::u8string_view labels[]{u8"ゲーム開始",u8"操作説明",u8"終了"};
            const float underlineWidths[]{180,144,72};
            const float center = x+232*s;
            for (int i=0;i<3;++i) {
                const float y=top+(480+i*82)*s;
                const bool selected=i==input.titleMenuSelection;
                // Keep the original generous mouse targets and row baselines;
                // the visual menu is lettering, not a stack of panels.
                text(Utf8(labels[i]),center+1.5f*s,y+46.5f*s,1.70f*s,
                    {0.025f,0.02f,0.015f,0.65f},RailShooterHudTextAlignment::Center);
                text(Utf8(labels[i]),center,y+45*s,1.70f*s,
                    selected?white:muted,RailShooterHudTextAlignment::Center);
                if(selected) {
                    text(">",center-(underlineWidths[i]*0.5f+28)*s,y+42*s,1.20f*s,accent);
                    rect(center-underlineWidths[i]*0.5f*s,y+59*s,
                        underlineWidths[i]*s,2*s,{accent.x,accent.y,accent.z,0.80f});
                }
            }
            text(Utf8(u8"W / S : 選択    ENTER : 決定"),center,top+778*s,0.76f*s,
                muted,RailShooterHudTextAlignment::Center);
            text(Utf8(u8"マウス : 選択 / 決定"),center,top+810*s,0.76f*s,
                muted,RailShooterHudTextAlignment::Center);
        } else {
            rect(x-16*s,top+456*s,654*s,377*s,{0.075f,0.06f,0.045f,0.95f});
            text(Utf8(u8"操作説明"),x,top+496*s,1.12f*s,accent);
            const std::u8string_view lines[]{
                u8"マウス : 照準    左クリック : 射撃",
                u8"右クリック長押し : ロックオン",
                u8"右クリックを離す : 一斉発射",
                u8"WASD : 移動    SHIFT / SPACE : 回避",
                u8"E 長押し : 加速    P : 一時停止"};
            for(int i=0;i<5;++i) text(Utf8(lines[i]),x,top+(546+i*46)*s,0.80f*s,white);
            text(Utf8(u8"ENTER / ESC / CLICK : 戻る"),x,top+809*s,0.77f*s,accent);
        }
        for(auto& command:next.commands) command.color.w *= (std::clamp)(input.titleOpacity,0.0f,1.0f);
        if(input.titleBlackout>0.0f) {
            RailShooterHudDrawCommand transition;
            transition.kind=RailShooterHudDrawCommandKind::TunnelTransition;
            transition.width=width; transition.height=height;
            transition.color={0.022f,0.017f,0.012f,1.0f};
            transition.transitionProgress=(std::clamp)(input.titleBlackout,0.0f,1.0f);
            transition.transitionFocus=input.titleTransitionFocus;
            // The world switch must be concealed even with a small UI budget.
            if(budget>0 && next.commands.size()>=budget) next.commands.pop_back();
            push(std::move(transition));
        }
        next.visible = !next.commands.empty();
        frame_ = std::move(next);
        return;
    }

    if(input.bossVisible && input.miniBossParts && !input.startCueActive) {
        const float w=540*scale,x=(width-w)*0.5f,y=height-158*scale;
        const auto center=RailShooterHudTextAlignment::Center;
        const int left=int(input.miniBossPartHealth[0]>0)+int(input.miniBossPartHealth[1]>0);
        text(Utf8(u8"母艦の武装を破壊  残り ")+std::to_string(left)+"/2",width*0.5f,y,
            0.70f*scale,{1,0.88f,0.62f,opacity},center);
        const bool counter=input.miniBossCounterRemaining>0 && input.miniBossCounterDuration>0;
        if(input.miniBossGatlingFiring && !counter) {
            text(Utf8(u8"通常弾を撃ち続けて相殺  ")+std::to_string(input.miniBossGatlingIntercepts)+"/24",
                width*.5f,y-35*scale,.75f*scale,{1,.83f,.5f,opacity},center);
        }
        if(counter) {
            text(input.miniBossCounterPart==0 ? Utf8(u8"迎撃成功  砲台へ戻して主砲で反撃 x2") :
                Utf8(u8"迎撃成功  ロックオンで反撃 x2"),width*.5f,y-35*scale,
                .82f*scale,{1,.86f,.42f,opacity},center);
            bar(width*.5f-135*scale,y-23*scale,270*scale,4*scale,
                (std::clamp)(input.miniBossCounterRemaining/input.miniBossCounterDuration,0.0f,1.0f),
                {1,.75f,.22f,opacity},{.13f,.10f,.05f,opacity});
        }
        if(input.miniBossCounterFeedbackRemaining>0 && input.miniBossCounterDamage>0) {
            text(Utf8(u8"反撃成功  ")+std::to_string(int(std::round(input.miniBossCounterDamage)))+" DAMAGE",
                width*.5f,y-59*scale,.82f*scale,{1,.88f,.45f,opacity},center);
        }
        for(int i=0;i<2;++i) {
            const float px=x+i*280*scale;
            const bool alive=input.miniBossPartHealth[i]>0;
            const bool exposed=counter && alive && input.miniBossCounterPart==i;
            text((i==0?Utf8(u8"ガトリング"):Utf8(u8"重砲"))+(alive?"":Utf8(u8" 破壊済み")),
                px,y+22*scale,0.65f*scale,alive?Vector4{0.85f,0.93f,1,opacity}:Vector4{0.5f,0.6f,0.6f,opacity});
            bar(px,y+29*scale,250*scale,6*scale,input.miniBossPartHealth[i],
                exposed?Vector4{1,.88f,.45f,opacity}:
                    i==0?Vector4{0.2f,0.8f,1,opacity}:Vector4{1,0.48f,0.16f,opacity},{0.1f,0.12f,0.14f,opacity});
        }
    } else if(input.bossVisible && !input.startCueActive) {
        const float w=580*scale,x=(width-w)*0.5f,y=height-110*scale;
        rect(x-12*scale,y-28*scale,w+24*scale,61*scale,{0.012f,0.018f,0.024f,0.86f*opacity});
        text("CARRIER / REACTOR",width*0.5f,y-6*scale,0.68f*scale,{1,0.82f,0.56f,opacity},RailShooterHudTextAlignment::Center);
        bar(x,y+5*scale,w,8*scale,input.bossHealth,{1,0.43f,0.16f,opacity},{0.16f,0.12f,0.10f,opacity});
    }
    const Vector4 panel = WithOpacity(definition.panelColor, opacity);
    const Vector4 textColor = WithOpacity(definition.textColor, opacity);
    const Vector4 muted = WithOpacity(definition.mutedColor, opacity);
    const Vector4 primary = WithOpacity(definition.primaryColor, opacity);
    const Vector4 healthy = WithOpacity(definition.healthyColor, opacity);
    const Vector4 warning = WithOpacity(definition.warningColor, opacity);
    const float criticalOpacity = opacity * (0.65f + 0.35f * hud.warningPulse);
    const Vector4 critical = WithOpacity(definition.criticalColor, criticalOpacity);
    const Vector4 barBackground{0.04f, 0.075f, 0.09f, opacity * 0.92f};

    // Thin stepped markers leave the aiming area clear.
    for(size_t direction=0;direction<4;++direction) {
        const float alpha=hud.damageDirectionAlpha[direction]*opacity;
        if(alpha<=0) continue;
        for(int band=0;band<3;++band) {
            const float inset=(8+band*5)*responsive;
            const float length=(88-band*18)*responsive;
            const Vector4 color{1.0f,0.28f+band*0.07f,0.12f,alpha*(1-band*0.24f)};
            if(direction<2) rect(direction==0?inset:width-inset-3*responsive,
                height*0.5f-length*0.5f,3*responsive,length,color);
            else rect(width*0.5f-length*0.5f,direction==2?inset:height-inset-3*responsive,
                length,3*responsive,color);
        }
    }


    const auto plate = [&](float x,float y,float w,float h,Vector4 color) {
        RailShooterHudDrawCommand command{};
        command.kind=RailShooterHudDrawCommandKind::Plate;
        command.x=x; command.y=y; command.width=w; command.height=h; command.color=color;
        push(std::move(command));
    };
    const auto ink = [&](const std::string& value,float x,float y,float size,Vector4 color,
                         RailShooterHudTextAlignment align=RailShooterHudTextAlignment::Left) {
        text(value,x+1.5f*scale,y+2*scale,size,{0.015f,0.02f,0.025f,color.w*0.8f},align);
        text(value,x,y,size,color,align);
    };
    const auto center=RailShooterHudTextAlignment::Center;
    const float gaugeWidth=definition.leftPanelWidth*scale;
    const float startupGauge=(std::clamp)(input.startupGaugeProgress,0.0f,1.0f);
    const size_t gaugeBegin=next.commands.size();
    float healthBottom=safe;
    // Vehicle survival leads. Player HP is a distinct, secondary gauge.
    if(definition.showVehicleIntegrity) {
        const float y=safe;
        const Vector4 color=hud.vehicleIntegrityCritical?critical:primary;
        // Small original cart silhouette; no external UI artwork.
        plate(safe,y+7*scale,32*scale,19*scale,color);
        rect(safe+6*scale,y+29*scale,6*scale,6*scale,textColor);
        rect(safe+23*scale,y+29*scale,6*scale,6*scale,textColor);
        ink(hud.vehicleText,safe+45*scale,y+28*scale,0.95f*scale,textColor);
        bar(safe,y+42*scale,gaugeWidth,18*scale,hud.vehicleIntegrityNormalized*startupGauge,
            color,barBackground,hud.vehicleIntegrityTrail*startupGauge);
        // Tick marks aid estimating remaining durability without reading digits.
        for(int i=1;i<5;++i) rect(safe+gaugeWidth*i/5.0f,y+42*scale,2*scale,18*scale,panel);
        healthBottom=y+60*scale;
    }
    if(definition.showPlayerHealth) {
        const float y=healthBottom+(definition.showVehicleIntegrity?24:0)*scale;
        ink(hud.healthText,safe,y+19*scale,0.68f*scale,textColor);
        bar(safe,y+29*scale,gaugeWidth*0.72f,8*scale,hud.playerHealthNormalized*startupGauge,
            hud.playerHealthCritical?critical:healthy,barBackground,hud.playerHealthTrail*startupGauge);
        healthBottom=y+37*scale;
    }
    const size_t gaugeEnd=next.commands.size();
    if(definition.showPlayerHealth || definition.showVehicleIntegrity) {
        text(input.unlimitedCheckpointRetries?Utf8(u8"リトライ 無制限"):Utf8(u8"残機 ")+std::to_string(hud.retriesRemaining),safe,
            healthBottom+23*scale,0.54f*scale,muted);
    }
    if(hud.showDamageNotice && hud.damageNoticeAlpha>0) {
        const float y=healthBottom+39*scale;
        const float a=opacity*hud.damageNoticeAlpha;
        // Event card sits alongside the health display, never over the reticle.
        plate(safe,y,365*scale,66*scale,{0.10f,0.025f,0.018f,a*0.85f});
        text(hud.damageNoticeText,safe+14*scale,y+26*scale,0.73f*scale,{1,0.64f,0.39f,a});
        text(hud.damageHealthText,safe+14*scale,y+51*scale,0.65f*scale,{1,0.97f,0.9f,a});
    }
    if(definition.showWaveObjective) {
        const float w=definition.topCenterWidth*0.64f*scale;
        ink(hud.waveText,width*0.5f,safe+19*scale,0.66f*scale,muted,center);
        bar(width*0.5f-w*0.5f,safe+31*scale,w,4*scale,
            hud.courseProgressNormalized,primary,barBackground);
    }
    if(definition.showScore) {
        const float x=width-safe-definition.rightPanelWidth*scale*0.5f;
        text(Utf8(u8"得点"),x,safe+16*scale,0.57f*scale,muted,center);
        const std::string digits=std::to_string(hud.score);
        const float numberSize=digits.size()>8?1.14f:1.75f;
        ink(digits,x,safe+60*scale,numberSize*scale,textColor,center);
        if(hud.combo>1) {
            plate(x-83*scale,safe+76*scale,166*scale,33*scale,warning);
            text(hud.comboText,x,safe+100*scale,0.90f*scale,
                {0.075f,0.055f,0.025f,opacity},center);
        }
        if(!hud.scoreGainText.empty() && hud.scoreGainAlpha>0) {
            const float a=(std::clamp)(hud.scoreGainAlpha,0.0f,1.0f);
            ink(hud.scoreGainText,x,safe+(146-8*(1-a))*scale,
                (1.04f+0.16f*a)*scale,{warning.x,warning.y,warning.z,opacity*a},center);
        }
        if(hud.grazeChain>0) text(hud.grazeText,x,safe+174*scale,
            0.58f*scale,primary,center);
        if(input.shortRun && !input.startCueActive && input.resultSession &&
            input.resultSession->phase==GameSessionPhase::Playing && input.encounterScore &&
            input.encounterScore->showcaseSecondsRemaining>0) {
            const auto& success=*input.encounterScore;
            std::string label;Vector4 accent=primary;
            switch(success.showcaseKind) {
            case EncounterPerformanceScoreKind::LockSweep: label=Utf8(u8"一斉撃破");break;
            case EncounterPerformanceScoreKind::BatteryBreak: label=Utf8(u8"砲台制圧");accent=warning;break;
            case EncounterPerformanceScoreKind::ProjectileIntercept: label=Utf8(u8"連続迎撃");break;
            default:break;
            }
            if(!label.empty()) {
                const float a=opacity*(std::min)(1.0f,success.showcaseSecondsRemaining/0.25f);
                const float y=safe+(200+4*(1-a))*scale;
                plate(x-128*scale,y,256*scale,35*scale,{0.015f,0.025f,0.032f,a*0.82f});
                rect(x-128*scale,y,2*scale,35*scale,{accent.x,accent.y,accent.z,a});
                const auto message=label+"  x"+std::to_string(success.showcaseCount)+"  +"+std::to_string(success.showcaseScore);
                ink(message,x,y+24*scale,0.73f*scale,{accent.x,accent.y,accent.z,a},center);
            }
        }
    }
    if(definition.showWeapon) {
        // Lock readiness is anchored below combat, separate from enemy markers.
        const float y=height-safe-67*scale;
        const uint32_t slots=(std::min)(hud.maximumLocks,8u);
        if(slots>0) {
            ink(Utf8(u8"ロック ")+std::to_string(hud.lockCount)+" / "+std::to_string(hud.maximumLocks),
                width*0.5f,y+21*scale,0.80f*scale,textColor,center);
            const float total=slots*22.0f*scale;
            if(hud.maximumLocks>slots) {
                bar(width*0.5f-total*0.5f,y+33*scale,total,10*scale,
                    static_cast<float>(hud.lockCount)/hud.maximumLocks,primary,barBackground);
            } else for(uint32_t i=0;i<slots;++i) {
                plate(width*0.5f-total*0.5f+i*22*scale,y+33*scale,17*scale,10*scale,
                    i<hud.lockCount?primary:Vector4{0.12f,0.15f,0.15f,opacity});
            }
        }
        const float x=width-safe-220*scale;
        ink(hud.weaponText,x,height-safe-48*scale,0.78f*scale,textColor);
        if(hud.primaryWeapon.overheated || hud.primaryWeapon.reloading ||
           !hud.primaryWeapon.unlimitedAmmo) {
            text(hud.weaponStatusText,x,height-safe-24*scale,0.64f*scale,
                hud.primaryWeapon.overheated?critical:warning);
        }
        if(hud.primaryWeapon.available && hud.primaryWeapon.heatNormalized>0.01f) {
            bar(x,height-safe-12*scale,210*scale,6*scale,hud.primaryWeapon.heatNormalized,
                hud.primaryWeapon.overheated?critical:warning,barBackground);
        }
    }
    if(definition.showSpeed) {
        text(hud.speedText,safe,height-safe-15*scale,0.60f*scale,muted);
    }
    if(hud.showObstacleWarning) {
        const float w=(std::min)(450*scale,width-2*safe);
        const float y=safe+63*scale;
        plate(width*0.5f-w*0.5f,y,w,67*scale,{0.10f,0.065f,0.025f,opacity*0.88f});
        text(hud.obstacleWarningText,width*0.5f,y+27*scale,0.78f*scale,warning,center);
        text(hud.obstacleActionText,width*0.5f,y+53*scale,0.72f*scale,textColor,center);
    } else if(definition.showThreat && hud.threatWarning) {
        ink(hud.threatText,width*0.5f,safe+76*scale,0.80f*scale,critical,center);
    }
    if(definition.showSessionBanner && !input.startCueActive && hud.showBanner && hud.bannerAlpha>0) {
        const float a=opacity*hud.bannerAlpha;
        const float y=height*0.27f;
        const float w=(std::min)(560*scale,width-2*safe);
        plate(width*0.5f-w*0.5f,y-39*scale,w,60*scale,{0.035f,0.045f,0.05f,a*0.86f});
        ink(hud.bannerHeadline,width*0.5f,y+6*scale,1.34f*scale,
            {hud.bannerColor.x,hud.bannerColor.y,hud.bannerColor.z,a},center);
        if(!hud.bannerDetail.empty()) ink(hud.bannerDetail,width*0.5f,y+50*scale,
            0.69f*scale,{1,0.98f,0.91f,a},center);
    }
    if(input.shortRun && input.resultSession && !input.startCueActive &&
        input.resultSession->phase==GameSessionPhase::Playing) {
        const float distance=input.resultSession->courseDistance;
        const int stage=distance<380?0:distance<680?1:distance<950?2:distance<1120?3:4;
        const char* stages[]={"FIRST CONTACT / LMB","LOCK BURST / HOLD RMB","INTERCEPT / SHOOT ORBS","BOOST RUN / HOLD E","FINAL PURSUIT"};
        const int remaining=int((std::max)(0.0f,ShortRunSessionDefinition().timeLimitSeconds-input.resultSession->attemptElapsedSeconds));
        text(std::to_string(stage+1)+" / 5   "+stages[stage],safe,height-90*responsive,0.64f*responsive,textColor);
        text("TIME  "+std::to_string(remaining),safe,height-64*responsive,0.58f*responsive,muted);
        if(distance>=input.resultSession->courseLength-2.0f && !input.resultSession->combatClearConfirmed && !input.miniBossParts)
            text("CLEAR REMAINING HOSTILES",width*0.5f,height*0.24f,0.85f*responsive,warning,center);
    }
    // Keep compact reminders at the edge, not a permanent panel across combat.
    if(width>=960) {
        text(Utf8(u8"左クリック : 射撃   右クリック : ロック / 離して発射   E : 加速   P : 一時停止"),
            width*0.5f,height-10*responsive,0.50f*responsive,muted,center);
    }
    for(size_t index=0;index<next.commands.size();++index) {
        auto& command=next.commands[index];
        if(index>=gaugeBegin && index<gaugeEnd) {
            command.color.w*=startupGauge;
            command.x-=(1.0f-startupGauge)*8.0f*scale;
        } else command.color.w*=(std::clamp)(input.startupInformationOpacity,0.0f,1.0f);
    }
    // The start cue has its own opacity, independent from ordinary HUD boot.
    // Keep the start cue and its small progress bar above the aiming area.
    const float readyAlpha = (std::clamp)(input.readyOpacity,0.0f,1.0f)*opacity;
    const float goAlpha = (std::clamp)(input.goOpacity,0.0f,1.0f)*opacity;
    const float cueY = height*0.25f;
    const auto startCue = [&](const char* word, float alpha, Vector4 color) {
        if(alpha<=0.0f) return;
        const size_t begin=next.commands.size();
        // One close, opaque shadow separates the face from both pale sky and
        // dark rock without a glow or a heavy extruded edge.
        text(word,width*0.5f+2.0f*scale,cueY+3.0f*scale,4.8f*scale,
            {0.025f,0.020f,0.015f,alpha*0.95f},center);
        color.w=alpha;
        text(word,width*0.5f,cueY,4.8f*scale,color,center);
        for(size_t i=begin;i<next.commands.size();++i)
            next.commands[i].textStyle=RailShooterHudTextStyle::StartCue;
    };
    startCue("READY",readyAlpha,{1.0f,0.98f,0.94f,1.0f});
    startCue("GO",goAlpha,{0.79f,1.0f,0.91f,1.0f});
    const float cueAlpha=(std::max)(readyAlpha,goAlpha);
    if(cueAlpha>0.0f) {
        // Use the shared cinematic clock: focus pause freezes the fill, and
        // GO completes it only after the camera and drone are in position.
        const float progress=goAlpha>0.0f ? 1.0f : (std::clamp)(input.readyProgress,0.0f,1.0f);
        const float w=240.0f*scale,h=6.0f*scale;
        const float x=width*0.5f-w*0.5f,y=cueY+24.0f*scale;
        const Vector4 fill=goAlpha>0.0f ? Vector4{0.60f,1.0f,0.80f,cueAlpha} :
            Vector4{0.96f,0.77f,0.43f,cueAlpha};
        rect(x-3*scale,y-3*scale,w+6*scale,h+6*scale,
            {0.018f,0.016f,0.013f,cueAlpha*0.78f});
        rect(x,y,w,h,{0.34f,0.31f,0.25f,cueAlpha*0.60f});
        rect(x,y,w*progress,h,fill);
        rect(x,y,w*progress,1.0f*scale,{1.0f,0.98f,0.88f,cueAlpha*0.65f});
        rect(x-5*scale,y-3*scale,1.0f*scale,h+6*scale,
            {0.92f,0.86f,0.72f,cueAlpha*0.85f});
        rect(x+w+4*scale,y-3*scale,1.0f*scale,h+6*scale,
            {0.92f,0.86f,0.72f,cueAlpha*0.85f});
    }
    std::erase_if(next.commands,[](const auto& command){return command.color.w<=0.0f;});
    next.visible = !next.commands.empty();
    frame_ = std::move(next);
}
