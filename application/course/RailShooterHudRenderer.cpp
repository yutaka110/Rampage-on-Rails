#include "RailShooterHudRenderer.h"

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
    if (input.definition == nullptr || input.presentation == nullptr ||
        (!input.showTitleScreen &&
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
                u8"P : 一時停止"};
            for(int i=0;i<5;++i) text(Utf8(lines[i]),x,top+(546+i*46)*s,0.80f*s,white);
            text(Utf8(u8"ENTER / ESC / CLICK : 戻る"),x,top+809*s,0.77f*s,accent);
        }
        for(auto& command:next.commands) command.color.w *= (std::clamp)(input.titleOpacity,0.0f,1.0f);
        rect(0,0,width,height,{0.015f,0.025f,0.035f,(std::clamp)(input.titleBlackout,0.0f,1.0f)});
        next.visible = !next.commands.empty();
        frame_ = std::move(next);
        return;
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
        bar(safe,y+42*scale,gaugeWidth,18*scale,hud.vehicleIntegrityNormalized,
            color,barBackground,hud.vehicleIntegrityTrail);
        // Tick marks aid estimating remaining durability without reading digits.
        for(int i=1;i<5;++i) rect(safe+gaugeWidth*i/5.0f,y+42*scale,2*scale,18*scale,panel);
        healthBottom=y+60*scale;
    }
    if(definition.showPlayerHealth) {
        const float y=healthBottom+(definition.showVehicleIntegrity?24:0)*scale;
        ink(hud.healthText,safe,y+19*scale,0.68f*scale,textColor);
        bar(safe,y+29*scale,gaugeWidth*0.72f,8*scale,hud.playerHealthNormalized,
            hud.playerHealthCritical?critical:healthy,barBackground,hud.playerHealthTrail);
        healthBottom=y+37*scale;
    }
    if(definition.showPlayerHealth || definition.showVehicleIntegrity) {
        text(Utf8(u8"残機 ")+std::to_string(hud.retriesRemaining),safe,
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
    if(definition.showSessionBanner && hud.showBanner && hud.bannerAlpha>0) {
        const float a=opacity*hud.bannerAlpha;
        const float y=height*0.27f;
        const float w=(std::min)(560*scale,width-2*safe);
        plate(width*0.5f-w*0.5f,y-39*scale,w,60*scale,{0.035f,0.045f,0.05f,a*0.86f});
        ink(hud.bannerHeadline,width*0.5f,y+6*scale,1.34f*scale,
            {hud.bannerColor.x,hud.bannerColor.y,hud.bannerColor.z,a},center);
        if(!hud.bannerDetail.empty()) ink(hud.bannerDetail,width*0.5f,y+50*scale,
            0.69f*scale,{1,0.98f,0.91f,a},center);
    }
    // Keep compact reminders at the edge, not a permanent panel across combat.
    if(width>=960) {
        text(Utf8(u8"左クリック : 射撃   右クリック : ロック / 離して発射   P : 一時停止"),
            width*0.5f,height-10*responsive,0.50f*responsive,muted,center);
    }
    next.visible = !next.commands.empty();
    frame_ = std::move(next);
}
