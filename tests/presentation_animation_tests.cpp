#include <mirrorfly/presentation.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    using namespace mirrorfly;
    int failures = 0;
    void check(bool value, const std::string& label)
    {
        if (!value)
        {
            std::cerr << label << '\n';
            ++failures;
        }
    }

    std::string effect(const std::string& type, const std::string& category, const std::string& target,
        const std::string& behavior, int delay = 0)
    {
        return "<p:par><p:cTn nodeType='" + type + "' presetClass='" + category +
            "' fill='hold'><p:stCondLst><p:cond delay='" + std::to_string(delay) +
            "'/></p:stCondLst><p:childTnLst>" + behavior +
            "<p:set><p:cBhvr><p:cTn dur='1'/><p:tgtEl><p:spTgt spid='" + target +
            "'/></p:tgtEl></p:cBhvr></p:set></p:childTnLst></p:cTn></p:par>";
    }

    int run_animation_tests()
    {
        auto native = make_presentation(PresentationSlideLayout::Title);
        native.slides.front().shapes.front().text.paragraphs.front().runs.front().text = "ONE TWO";
        auto package = serialize_presentation(native);
        const auto fade = effect("clickEffect", "entr", "2",
            "<p:animEffect filter='fade' transition='in'><p:cBhvr><p:cTn "
            "dur='1000'/></p:cBhvr></p:animEffect>");
        const auto wipe = effect("withEffect", "entr", "3",
            "<p:animEffect filter='wipe(right)' transition='in'><p:cBhvr><p:cTn "
            "dur='2000'/></p:cBhvr></p:animEffect>",
            250);
        const auto spin = effect("afterEffect", "emph", "2",
            "<p:animRot by='21600000'><p:cBhvr><p:cTn dur='1000'/></p:cBhvr></p:animRot>", 100);
        const auto motion = effect("clickEffect", "path", "3",
            "<p:animMotion path='M 0 0 L 0.5 0 C 0.6 0 0.7 0.1 0.8 0.2 E'><p:cBhvr><p:cTn "
            "dur='1000'/></p:cBhvr></p:animMotion>");
        const auto exit = effect("afterEffect", "exit", "2",
            "<p:animEffect filter='fade' transition='out'><p:cBhvr><p:cTn "
            "dur='500'/></p:cBhvr></p:animEffect>");
        const auto play = effect("clickEffect", "mediacall", "2",
            "<p:cmd type='call' cmd='playFrom(0.25)'><p:cBhvr><p:cTn dur='1'/>"
            "<p:tgtEl><p:spTgt spid='2'/></p:tgtEl></p:cBhvr></p:cmd>");
        const auto pause = effect("afterEffect", "mediacall", "2",
            "<p:cmd type='call' cmd='pause'><p:cBhvr><p:cTn dur='1'/>"
            "<p:tgtEl><p:spTgt spid='2'/></p:tgtEl></p:cBhvr></p:cmd>",
            500);
        const std::string automatic =
            "<p:video><p:cMediaNode vol='40000' numSld='3'><p:cTn dur='media' repeatCount='indefinite'>"
            "<p:stCondLst><p:cond delay='250'/></p:stCondLst></p:cTn>"
            "<p:tgtEl><p:spTgt spid='3'/></p:tgtEl></p:cMediaNode></p:video>"
            "<p:audio><p:cMediaNode vol='25000' numSld='2'><p:cTn dur='media'>"
            "<p:stCondLst><p:cond delay='indefinite'/></p:stCondLst></p:cTn>"
            "<p:tgtEl><p:spTgt spid='2'/></p:tgtEl></p:cMediaNode></p:audio>";
        std::string paragraph = effect("withEffect", "entr", "77",
            "<p:animEffect filter='fade' transition='in'><p:cBhvr><p:cTn "
            "dur='1000'/></p:cBhvr></p:animEffect>");
        const std::string plain_target = "<p:spTgt spid='77'/>";
        paragraph.replace(paragraph.find(plain_target), plain_target.size(),
            "<p:spTgt spid='77'><p:txEl><p:pRg st='0' end='1'/></p:txEl></p:spTgt>");
        std::string character = effect("withEffect", "entr", "77",
            "<p:animEffect filter='fade' transition='in'><p:cBhvr><p:cTn "
            "dur='1000'/></p:cBhvr></p:animEffect>");
        character.replace(character.find(plain_target), plain_target.size(),
            "<p:spTgt spid='77'><p:txEl><p:charRg st='1' end='4'/></p:txEl></p:spTgt>");
        std::string iterated = effect("withEffect", "entr", "2",
            "<p:animEffect filter='fade' transition='in'><p:cBhvr><p:cTn "
            "dur='1000'/></p:cBhvr></p:animEffect>");
        iterated.insert(iterated.find("<p:stCondLst>"),
            "<p:iterate type='wd' backwards='1'><p:tmPct val='50000'/></p:iterate>");
        std::string iterated_letters = effect("withEffect", "entr", "2",
            "<p:animEffect filter='fade' transition='in'><p:cBhvr><p:cTn "
            "dur='1000'/></p:cBhvr></p:animEffect>");
        iterated_letters.insert(
            iterated_letters.find("<p:stCondLst>"), "<p:iterate type='lt'><p:tmAbs val='200'/></p:iterate>");
        std::string iterated_elements = effect("withEffect", "entr", "2",
            "<p:animEffect filter='fade' transition='in'><p:cBhvr><p:cTn "
            "dur='1000'/></p:cBhvr></p:animEffect>");
        iterated_elements.insert(iterated_elements.find("<p:stCondLst>"),
            "<p:iterate type='el'><p:tmPct val='25000'/></p:iterate>");
        const std::string interactive =
            "<p:seq><p:cTn nodeType='interactiveSeq'><p:stCondLst>"
            "<p:cond evt='onClick' delay='0'><p:tgtEl><p:spTgt spid='2'/></p:tgtEl></p:cond>"
            "</p:stCondLst><p:childTnLst>" +
            effect("clickEffect", "entr", "99",
                "<p:animEffect filter='fade' transition='in'><p:cBhvr><p:cTn "
                "dur='1000'/></p:cBhvr></p:animEffect>") +
            paragraph + character + iterated + iterated_letters + iterated_elements +
            "</p:childTnLst></p:cTn></p:seq>";
        for (auto& part : package.parts)
            if (part.path == "ppt/slides/slide1.xml")
                part.bytes.insert(part.bytes.find("</p:sld>"),
                    "<p:timing><p:tnLst><p:par><p:cTn nodeType='tmRoot'>"
                    "<p:childTnLst><p:seq><p:cTn nodeType='mainSeq'><p:childTnLst>" +
                        fade + wipe + spin + motion + exit + play + pause +
                        "</p:childTnLst></p:cTn></p:seq>" + automatic + interactive +
                        "</p:childTnLst></p:cTn></p:par></p:tnLst></"
                        "p:timing>");
        auto parsed = parse_presentation(package.parts);
        check(parsed.error == PresentationError::None, "animation fixture parses");
        const auto& animations = parsed.scene.slides.front().animations;
        check(animations.size() == 11,
            "main, object-triggered, paragraph, character and iterated effects are retained");
        if (animations.size() != 11)
            return 1;
        const auto& cues = parsed.scene.slides.front().media_cues;
        check(cues.size() == 3, "play pause and automatic cues exclude indefinite media start");
        if (cues.size() == 3)
        {
            check(cues[0].click == 3 && cues[0].action == "play" && cues[0].position == 0.25 &&
                    cues[0].volume == 0.25 && cues[0].slide_count == 2,
                "media command reads click group start position, clip volume and cross-slide count");
            check(cues[1].click == 3 && cues[1].action == "pause" && std::abs(cues[1].delay - 0.501) < 1e-8,
                "media command follows sequence delay");
            check(cues[2].click == 0 && cues[2].delay == 0.25 && cues[2].target == "3" &&
                    cues[2].volume == 0.4 && cues[2].loop && cues[2].slide_count == 3,
                "automatic media starts independently and retains its cross-slide count");
        }
        check(animations[0].click == 1 && animations[1].click == 1 && animations[3].click == 2,
            "click groups distinguish click / with / after");
        check(std::abs(animations[1].delay - 0.25) < 1e-8 && std::abs(animations[2].delay - 2.35) < 1e-8,
            "parallel effects and subsequent duration determine sequence offset");
        check(animations[3].motion.size() == 26 && animations[3].motion.back()[0] == 0.8,
            "motion path includes straight and cubic coordinates");
        auto state = presentation_animation_state(animations, "2", {}, 5, {0}, 960, 540);
        check(state.opacity == 0, "entrance hidden before click");
        state = presentation_animation_state(animations, "2", {}, 5.5, {0, 5}, 960, 540);
        check(std::abs(state.opacity - 0.5) < 1e-8, "fade interpolation follows click time");
        state = presentation_animation_state(animations, "3", {}, 6.25, {0, 5}, 960, 540);
        check(state.clip == "wipe(right)" && std::abs(state.reveal - 0.5) < 1e-8,
            "wipe delay and reveal interpolation");
        state = presentation_animation_state(animations, "2", {}, 7.85, {0, 5}, 960, 540);
        check(std::abs(state.rotation - 180) < 1e-7 && state.opacity == 1,
            "emphasis begins after parallel effects finish");
        state = presentation_animation_state(animations, "3", {}, 11, {0, 5, 10}, 960, 540);
        check(std::abs(state.x - 768) < 1e-7 && std::abs(state.y - 108) < 1e-7,
            "motion uses slide-relative coordinates");
        state = presentation_animation_state(animations, "2", {}, 12, {0, 5, 10}, 960, 540);
        check(state.opacity == 0, "exit stays hidden after completion");
        state = presentation_animation_state(animations, "child", {"2"}, 5.5, {0, 5}, 960, 540);
        check(state.opacity == 0.5, "group target affects its descendants");
        state = presentation_animation_state(animations, "unrelated", {}, 5.5, {0, 5}, 960, 540);
        check(state.opacity == 1 && state.rotation == 0, "unrelated objects remain unchanged");
        check(animations[5].target == "99" && animations[5].trigger == "2" && animations[5].click == 0,
            "interactive sequence retains its trigger object and local click group");
        state = presentation_animation_state(animations, "99", {}, 5, {0}, 960, 540);
        check(state.opacity == 0, "interactive entrance remains hidden before its object trigger");
        state = presentation_animation_state(animations, "99", {}, 5.5, {0}, 960, 540, {{"2", {5}}});
        check(std::abs(state.opacity - 0.5) < 1e-8,
            "object trigger starts the independent interactive timeline");
        check(animations[6].target == "77" && animations[6].paragraph_start == 0 &&
                animations[6].paragraph_end == 1,
            "paragraph animation retains its inclusive text range");
        state = presentation_animation_state(animations, "77", {}, 5.5, {0}, 960, 540, {{"2", {5}}});
        check(state.opacity == 1, "paragraph animation does not hide the complete shape");
        state = presentation_animation_state(animations, "77", {}, 5.5, {0}, 960, 540, {{"2", {5}}}, 0);
        check(std::abs(state.opacity - 0.5) < 1e-8, "targeted paragraph follows its own fade timeline");
        state = presentation_animation_state(animations, "77", {}, 5.5, {0}, 960, 540, {{"2", {5}}}, 2);
        check(state.opacity == 1, "paragraphs outside the target range remain visible");
        check(animations[7].target == "77" && animations[7].character_start == 1 &&
                animations[7].character_end == 4,
            "character animation retains its exclusive text range");
        PresentationAnimationTextPosition character_position;
        character_position.character = 2;
        state = presentation_animation_state(
            animations, "77", {}, 5.5, {0}, 960, 540, {{"2", {5}}}, -1, character_position);
        check(std::abs(state.opacity - 0.5) < 1e-8, "targeted character follows its own fade timeline");
        character_position.character = 4;
        state = presentation_animation_state(
            animations, "77", {}, 5.5, {0}, 960, 540, {{"2", {5}}}, -1, character_position);
        check(state.opacity == 1, "characters at and after the exclusive range end remain visible");
        check(std::find(parsed.scene.slides.front().warnings.begin(),
                  parsed.scene.slides.front().warnings.end(),
                  "逐字动画暂按静态文字显示；原始文字范围和时间轴保留。") ==
                parsed.scene.slides.front().warnings.end(),
            "supported character ranges do not report a static fallback");
        check(animations[8].target == "2" && animations[8].iterate_type == "wd" &&
                animations[8].iterate_interval_percent &&
                std::abs(animations[8].iterate_interval - 0.5) < 1e-8 && animations[8].iterate_backwards &&
                animations[8].iterate_count == 2,
            "word iteration retains direction, interval and target word count");
        PresentationAnimationTextPosition word_position;
        word_position.character = 0;
        word_position.word = 0;
        word_position.word_count = 2;
        state = presentation_animation_state(
            animations, "2", {}, 5.25, {0}, 960, 540, {{"2", {5}}}, -1, word_position);
        check(state.opacity == 0, "backwards word iteration delays the first word");
        word_position.character = 4;
        word_position.word = 1;
        state = presentation_animation_state(
            animations, "2", {}, 5.25, {0}, 960, 540, {{"2", {5}}}, -1, word_position);
        check(std::abs(state.opacity - 0.25) < 1e-8, "backwards word iteration starts from the final word");
        state = presentation_animation_state(
            animations, "2", {}, 6.2, {0}, 960, 540, {{"2", {5}}}, -1, word_position);
        check(state.opacity == 1, "completed iteration unit holds instead of restarting during later units");
        check(animations[9].iterate_type == "lt" && !animations[9].iterate_interval_percent &&
                std::abs(animations[9].iterate_interval - 0.2) < 1e-8 && animations[9].iterate_count == 6,
            "letter iteration retains its absolute interval and target letter count");
        PresentationAnimationTextPosition letter_position;
        letter_position.character = 2;
        letter_position.letter = 2;
        letter_position.letter_count = 6;
        state = presentation_animation_state(
            animations, "2", {}, 5.3, {0}, 960, 540, {{"2", {5}}}, -1, letter_position);
        check(state.opacity == 0, "letter iteration waits for its absolute stagger interval");
        state = presentation_animation_state(
            animations, "2", {}, 5.5, {0}, 960, 540, {{"2", {5}}}, -1, letter_position);
        check(std::abs(state.opacity - 0.1) < 1e-8, "letter iteration starts after its absolute interval");
        check(animations[10].iterate_type == "el" && animations[10].iterate_interval_percent &&
                std::abs(animations[10].iterate_interval - 0.25) < 1e-8 && animations[10].iterate_count == 1,
            "element iteration retains its percentage interval and paragraph count");
        PresentationAnimationTextPosition element_position;
        element_position.character = 0;
        element_position.element = 0;
        element_position.element_count = 1;
        state = presentation_animation_state(
            animations, "2", {}, 5.5, {0}, 960, 540, {{"2", {5}}}, -1, element_position);
        check(std::abs(state.opacity - 0.5) < 1e-8, "element iteration applies to its paragraph text");

        PresentationAnimation repeated;
        repeated.target = "repeat";
        repeated.filter = "spin";
        repeated.category = "emph";
        repeated.duration = 1;
        repeated.rotation = 360;
        repeated.reverse = true;
        repeated.repeat = 2;
        state = presentation_animation_state({repeated}, "repeat", {}, 1.5, {0}, 960, 540);
        check(state.rotation == 180, "auto reverse returns toward initial rotation");
        state = presentation_animation_state({repeated}, "repeat", {}, 4, {0}, 960, 540);
        check(state.rotation == 0, "repeated auto reverse ends at initial state");
        repeated.reverse = false;
        repeated.hold = false;
        state = presentation_animation_state({repeated}, "repeat", {}, 3, {0}, 960, 540);
        check(state.rotation == 0, "remove fill resets completed emphasis");
        parsed.scene.native_editable = true;
        const auto restored = parse_presentation(serialize_presentation(parsed.scene).parts);
        check(restored.scene.slides.front().animations.size() == 11, "copy preserves complete timing tree");
        check(restored.scene.slides.front().media_cues.size() == 3, "copy preserves media timing commands");
        check(presentation_animation_state(
                  animations, "2", {}, std::numeric_limits<double>::quiet_NaN(), {0}, 960, 540)
                    .opacity == 1,
            "invalid clock does not produce invalid geometry");
        return failures ? 1 : 0;
    }
}

int main()
{
    return run_animation_tests();
}
