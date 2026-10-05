#include "presentation_animation_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <locale>
#include <set>
#include <sstream>
#include <utf8.h>

namespace
{
    using Node = pugi::xml_node;
    using Animation = mirrorfly::PresentationAnimation;

    std::string local(const char* name)
    {
        const auto colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    Node child(Node parent, const std::string& name)
    {
        for (auto item : parent.children())
            if (local(item.name()) == name)
                return item;
        return {};
    }

    void walk(Node root, const std::function<void(Node)>& visitor, int depth = 0)
    {
        if (depth > 64)
            return;
        visitor(root);
        for (auto item : root.children())
            walk(item, visitor, depth + 1);
    }

    double number(const char* text, double fallback, double limit = 100000000)
    {
        if (!text || !*text)
            return fallback;
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        return end != text && *end == 0 && std::isfinite(value) && std::abs(value) <= limit ? value
                                                                                            : fallback;
    }

    std::vector<std::array<double, 2>> motion_points(const std::string& path)
    {
        if (path.size() > 65536)
            return {};
        std::string spaced;
        for (const char character : path)
        {
            if (std::strchr("MLCQZEm lcqz", character) && character != ' ')
            {
                spaced += ' ';
                spaced += character;
                spaced += ' ';
            }
            else
                spaced += character == ',' ? ' ' : character;
        }
        std::istringstream stream(spaced);
        stream.imbue(std::locale::classic());
        std::vector<std::array<double, 2>> result;
        std::array<double, 2> current{0, 0};
        char command = 0;
        while (stream >> command)
        {
            if (result.size() > 2048)
                return {};
            if (command == 'E')
                break;
            if (command == 'Z' || command == 'z')
            {
                if (!result.empty())
                    result.push_back(result.front());
                continue;
            }
            const bool relative = command >= 'a' && command <= 'z';
            const char kind = relative ? static_cast<char>(command - 'a' + 'A') : command;
            const int count = kind == 'C' ? 3 : (kind == 'Q' ? 2 : (kind == 'M' || kind == 'L' ? 1 : 0));
            if (!count)
                return {};
            std::array<std::array<double, 2>, 3> points{};
            for (int index = 0; index < count; ++index)
                for (int coordinate = 0; coordinate < 2; ++coordinate)
                {
                    if (!(stream >> points[index][coordinate]) || !std::isfinite(points[index][coordinate]) ||
                        std::abs(points[index][coordinate]) > 100)
                        return {};
                    if (relative)
                        points[index][coordinate] += current[coordinate];
                }
            if (count == 1)
                result.push_back(points[0]);
            else
                for (int step = 1; step <= 24; ++step)
                {
                    const double t = step / 24.0;
                    const double u = 1 - t;
                    std::array<double, 2> point{};
                    for (int axis = 0; axis < 2; ++axis)
                    {
                        if (count == 3)
                            point[axis] = u * u * u * current[axis] + 3 * u * u * t * points[0][axis] +
                                3 * u * t * t * points[1][axis] + t * t * t * points[2][axis];
                        else
                            point[axis] =
                                u * u * current[axis] + 2 * u * t * points[0][axis] + t * t * points[1][axis];
                    }
                    result.push_back(point);
                }
            current = points[count - 1];
        }
        return result;
    }

    void warn(mirrorfly::PresentationSlide& slide, const std::string& message)
    {
        if (slide.warnings.size() < 80 &&
            std::find(slide.warnings.begin(), slide.warnings.end(), message) == slide.warnings.end())
            slide.warnings.push_back(message);
    }

    double condition_delay(Node node)
    {
        double delay = 0;
        for (auto condition : child(node, "stCondLst").children())
            delay = std::max(delay, number(condition.attribute("delay").value(), 0) / 1000);
        return std::clamp(delay, 0.0, 3600.0);
    }

    double span(const Animation& animation)
    {
        const double base = animation.duration * animation.repeat * (animation.reverse ? 2 : 1);
        const double interval = animation.iterate_interval_percent
            ? animation.duration * animation.iterate_interval
            : animation.iterate_interval;
        return base + std::max(0, animation.iterate_count - 1) * interval;
    }

    bool animation_space(std::uint32_t character)
    {
        return character <= 0x20 || character == 0x85 || character == 0xA0 || character == 0x1680 ||
            (character >= 0x2000 && character <= 0x200A) || character == 0x2028 || character == 0x2029 ||
            character == 0x202F || character == 0x205F || character == 0x3000;
    }

    int text_iteration_count(
        const mirrorfly::PresentationSlide& slide, const std::string& target, const std::string& type)
    {
        const auto shape = std::find_if(slide.shapes.begin(), slide.shapes.end(), [&](const auto& item)
        {
            return item.source_id == target;
        });
        if (shape == slide.shapes.end())
            return 0;
        if (type == "el")
            return static_cast<int>(shape->text.paragraphs.size());
        int letters = 0;
        int words = 0;
        bool in_word = false;
        try
        {
            for (const auto& paragraph : shape->text.paragraphs)
            {
                in_word = false;
                for (const auto& run : paragraph.runs)
                {
                    auto position = run.text.begin();
                    while (position != run.text.end())
                    {
                        const auto code = utf8::next(position, run.text.end());
                        const bool space = animation_space(code);
                        if (!space)
                        {
                            ++letters;
                            if (!in_word)
                                ++words;
                        }
                        in_word = !space;
                    }
                }
            }
        }
        catch (const utf8::exception&)
        {
            return 0;
        }
        return type == "lt" ? letters : words;
    }

    struct Sequence
    {
        int click = 0;
        double start = 0;
        double end = 0;
    };

    bool read_media_command(Node command, mirrorfly::PresentationMediaCue& cue)
    {
        if (std::string(command.attribute("type").value()) != "call")
            return false;
        const std::string action = command.attribute("cmd").value();
        if (action == "play" || action == "pause" || action == "stop")
            cue.action = action;
        else if (action.size() > 10 && action.rfind("playFrom(", 0) == 0 && action.back() == ')')
        {
            cue.position = number(action.substr(9, action.size() - 10).c_str(), -1, 86400);
            if (cue.position < 0)
                return false;
            cue.action = "play";
        }
        else
            return false;
        cue.target = child(child(child(command, "cBhvr"), "tgtEl"), "spTgt").attribute("spid").value();
        return !cue.target.empty();
    }

    void append_media_cue(mirrorfly::PresentationSlide& slide, const mirrorfly::PresentationMediaCue& cue)
    {
        if (slide.media_cues.size() < 256)
            slide.media_cues.push_back(cue);
        else
            warn(slide, "媒体时间轴超过 256 条指令播放上限；原始定义保留。");
    }

    void read_media_node(Node node, mirrorfly::PresentationSlide& slide, const Sequence& sequence,
        double delay, const std::string& trigger)
    {
        const auto common = child(node, "cMediaNode");
        const auto timing = child(common, "cTn");
        for (auto condition : child(timing, "stCondLst").children())
            if (std::string(condition.attribute("delay").value()) == "indefinite" ||
                condition.attribute("evt"))
                return;
        mirrorfly::PresentationMediaCue cue;
        cue.target = child(child(common, "tgtEl"), "spTgt").attribute("spid").value();
        if (cue.target.empty())
            return;
        cue.slide_count = std::clamp(common.attribute("numSld").as_int(1), 1, 100000);
        cue.click = trigger.empty() ? 0 : std::max(sequence.click, 0);
        cue.delay = delay + condition_delay(timing);
        cue.position = 0;
        cue.trigger = trigger;
        append_media_cue(slide, cue);
        const double duration = number(timing.attribute("dur").value(), 0) / 1000;
        if (duration > 0)
        {
            cue.action = "stop";
            cue.delay += duration;
            append_media_cue(slide, cue);
        }
    }

    void read_effect(Node time_node, mirrorfly::PresentationSlide& slide, Sequence& sequence,
        double parent_delay, const std::string& trigger)
    {
        Animation animation;
        animation.category = time_node.attribute("presetClass").value();
        const std::string node_type = time_node.attribute("nodeType").value();
        if (node_type == "clickEffect")
        {
            ++sequence.click;
            sequence.start = 0;
            sequence.end = 0;
        }
        animation.click = trigger.empty() ? sequence.click : std::max(sequence.click, 0);
        animation.trigger = trigger;
        animation.delay = (node_type == "afterEffect" ? sequence.end : sequence.start) +
            condition_delay(time_node) + parent_delay;
        animation.reverse = time_node.attribute("autoRev").as_bool();
        animation.repeat =
            std::clamp(number(time_node.attribute("repeatCount").value(), 1000) / 1000, 1.0, 1000.0);
        const std::string fill = time_node.attribute("fill").value();
        animation.hold = fill != "remove" && fill != "reset";
        animation.duration = 0;
        std::set<std::string> targets;
        bool unsupported_text_range = false;
        bool unsupported_behavior = false;
        bool visual_behavior = false;
        std::vector<mirrorfly::PresentationMediaCue> media_cues;
        walk(time_node, [&](Node node)
        {
            const auto name = local(node.name());
            if (name == "spTgt")
                targets.insert(node.attribute("spid").value());
            else if (name == "pRg")
            {
                const int start = node.attribute("st").as_int(-1);
                const int end = node.attribute("end").as_int(-1);
                if (start >= 0 && end >= start && end <= 100000)
                {
                    animation.paragraph_start = start;
                    animation.paragraph_end = end;
                }
                else
                    unsupported_text_range = true;
            }
            else if (name == "charRg")
            {
                const int start = node.attribute("st").as_int(-1);
                const int end = node.attribute("end").as_int(-1);
                if (start >= 0 && end > start && end <= 1000000)
                {
                    animation.character_start = start;
                    animation.character_end = end;
                }
                else
                    unsupported_text_range = true;
            }
            else if (name == "iterate")
            {
                const std::string type = node.attribute("type").value();
                if (type == "el" || type == "wd" || type == "lt")
                {
                    animation.iterate_type = type;
                    animation.iterate_backwards = node.attribute("backwards").as_bool();
                    const auto percentage = child(node, "tmPct");
                    const auto absolute = child(node, "tmAbs");
                    if (percentage)
                    {
                        animation.iterate_interval =
                            std::clamp(number(percentage.attribute("val").value(), 0) / 100000, 0.0, 10.0);
                        animation.iterate_interval_percent = true;
                    }
                    else if (absolute)
                    {
                        animation.iterate_interval =
                            std::clamp(number(absolute.attribute("val").value(), 0) / 1000, 0.0, 3600.0);
                    }
                }
                else
                    unsupported_text_range = true;
            }
            else if (name == "cTn" && node != time_node)
                animation.duration = std::max(animation.duration,
                    number(node.attribute("dur").value(), 0) / 1000 + condition_delay(node));
            else if (name == "animEffect")
            {
                visual_behavior = true;
                animation.filter = node.attribute("filter").value();
                if (animation.category.empty())
                    animation.category =
                        std::string(node.attribute("transition").value()) == "out" ? "exit" : "entr";
            }
            else if (name == "animMotion")
            {
                visual_behavior = true;
                animation.motion = motion_points(node.attribute("path").value());
                animation.filter = "motion";
                if (animation.motion.empty())
                    unsupported_behavior = true;
            }
            else if (name == "animRot")
            {
                visual_behavior = true;
                animation.rotation =
                    number(node.attribute("by").value(), number(node.attribute("to").value(), 0)) / 60000;
                animation.filter = "spin";
            }
            else if (name == "animScale")
            {
                visual_behavior = true;
                auto scale = child(node, "by");
                if (!scale)
                    scale = child(node, "to");
                animation.scale = {number(scale.attribute("x").value(), 100000) / 100000,
                    number(scale.attribute("y").value(), 100000) / 100000};
                animation.filter = "grow";
            }
            else if (name == "animClr")
            {
                visual_behavior = true;
                const auto color = child(child(node, "to"), "srgbClr");
                const std::string value = color.attribute("val").value();
                if (value.size() == 6 &&
                    value.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
                    animation.color = "#" + value;
                else
                    unsupported_behavior = true;
            }
            else if (name == "cmd")
            {
                mirrorfly::PresentationMediaCue cue;
                if (read_media_command(node, cue))
                {
                    cue.click = animation.click;
                    cue.delay = animation.delay + condition_delay(child(child(node, "cBhvr"), "cTn"));
                    cue.trigger = trigger;
                    media_cues.push_back(cue);
                }
                else
                    unsupported_behavior = true;
            }
        });
        const auto duration = number(time_node.attribute("dur").value(), 0) / 1000;
        animation.duration = std::clamp(std::max(duration, animation.duration), 0.001, 3600.0);
        const auto preset = time_node.attribute("presetID").as_int();
        const auto subtype = time_node.attribute("presetSubtype").as_int();
        if (animation.filter == "appear" && (animation.category == "entr" || animation.category == "exit"))
        {
            if (preset == 2)
            {
                animation.filter = "fly";
                if (subtype == 1)
                    animation.direction = {-1, 0};
                else if (subtype == 2)
                    animation.direction = {0, -1};
                else if (subtype == 4)
                    animation.direction = {1, 0};
                else if (subtype == 8)
                    animation.direction = {0, 1};
                else if (subtype == 3)
                    animation.direction = {-1, -1};
                else if (subtype == 6)
                    animation.direction = {1, -1};
                else if (subtype == 9)
                    animation.direction = {-1, 1};
                else if (subtype == 12)
                    animation.direction = {1, 1};
            }
            else if (preset == 10)
                animation.filter = "fade";
            else if (preset == 23)
                animation.filter = "zoom";
            else if (preset != 1)
                unsupported_behavior = true;
        }
        const auto filter_base = animation.filter.substr(0, animation.filter.find('('));
        const std::set<std::string> filters{"appear", "fade", "wipe", "blinds", "checkerboard", "wheel",
            "barn", "box", "circle", "diamond", "plus", "randomBars", "dissolve", "slide", "strips", "fly",
            "zoom", "spin", "grow", "motion"};
        if (!filters.count(filter_base))
        {
            animation.filter = "fade";
            unsupported_behavior = true;
        }
        if ((animation.character_start >= 0 || !animation.iterate_type.empty()) && filter_base != "appear" &&
            filter_base != "fade" && animation.color.empty())
            unsupported_behavior = true;
        sequence.start = animation.delay;
        sequence.end = std::max(sequence.end, animation.delay + span(animation));
        if (unsupported_text_range)
            warn(slide, "逐字动画暂按静态文字显示；原始文字范围和时间轴保留。");
        if (unsupported_behavior)
            warn(slide, "部分复杂动画参数使用近似效果或静态状态；原始时间轴保留。");
        for (const auto& cue : media_cues)
            append_media_cue(slide, cue);
        if (!media_cues.empty() && !visual_behavior)
            return;
        if (unsupported_text_range && animation.paragraph_start < 0 && animation.character_start < 0 &&
            animation.iterate_type.empty())
            return;
        for (const auto& target : targets)
        {
            if (slide.animations.size() >= 2048)
            {
                warn(slide, "页面动画数量超过播放上限；原始时间轴保留。");
                break;
            }
            if (target.empty())
                continue;
            auto targeted = animation;
            targeted.target = target;
            if (!targeted.iterate_type.empty())
                targeted.iterate_count = text_iteration_count(slide, target, targeted.iterate_type);
            sequence.end = std::max(sequence.end, targeted.delay + span(targeted));
            slide.animations.push_back(std::move(targeted));
        }
    }

    std::string click_trigger(Node time_node)
    {
        for (auto condition : child(time_node, "stCondLst").children())
        {
            if (std::string(condition.attribute("evt").value()) != "onClick")
            {
                continue;
            }
            const std::string target = child(child(condition, "tgtEl"), "spTgt").attribute("spid").value();
            if (!target.empty())
            {
                return target;
            }
        }
        return {};
    }

    void read_sequence(Node node, mirrorfly::PresentationSlide& slide, Sequence& sequence, double delay,
        int depth, const std::string& trigger = {})
    {
        if (depth > 64 || slide.animations.size() >= 2048)
            return;
        const auto name = local(node.name());
        if (name == "video" || name == "audio")
        {
            read_media_node(node, slide, sequence, delay, trigger);
            return;
        }
        if (local(node.name()) == "cTn")
        {
            const std::string type = node.attribute("nodeType").value();
            if (type == "interactiveSeq")
            {
                const std::string target = click_trigger(node);
                if (target.empty())
                {
                    warn(slide, "部分交互动画缺少可识别的单击目标；原始时间轴保留。");
                    return;
                }
                Sequence interactive;
                interactive.click = -1;
                for (auto item : child(node, "childTnLst").children())
                {
                    read_sequence(item, slide, interactive, delay + condition_delay(node), depth + 1, target);
                }
                return;
            }
            if (node.attribute("presetClass") || type == "clickEffect" || type == "withEffect" ||
                type == "afterEffect")
            {
                read_effect(node, slide, sequence, delay, trigger);
                return;
            }
            delay += condition_delay(node);
        }
        for (auto item : node.children())
            read_sequence(item, slide, sequence, delay, depth + 1, trigger);
    }

    std::array<double, 2> motion_point(const std::vector<std::array<double, 2>>& points, double progress)
    {
        if (points.empty())
            return {0, 0};
        double length = 0;
        for (std::size_t index = 1; index < points.size(); ++index)
            length +=
                std::hypot(points[index][0] - points[index - 1][0], points[index][1] - points[index - 1][1]);
        double remaining = length * progress;
        for (std::size_t index = 1; index < points.size(); ++index)
        {
            const double segment =
                std::hypot(points[index][0] - points[index - 1][0], points[index][1] - points[index - 1][1]);
            if (segment > 0 && remaining <= segment)
            {
                const double fraction = remaining / segment;
                return {points[index - 1][0] + (points[index][0] - points[index - 1][0]) * fraction,
                    points[index - 1][1] + (points[index][1] - points[index - 1][1]) * fraction};
            }
            remaining -= segment;
        }
        return points.back();
    }
}

namespace mirrorfly
{
    void read_presentation_animations(pugi::xml_node root, PresentationSlide& slide)
    {
        Sequence sequence;
        read_sequence(child(root, "timing"), slide, sequence, 0, 0);
        walk(child(root, "timing"), [&](Node node)
        {
            if (local(node.name()) != "cMediaNode")
                return;
            const std::string target = child(child(node, "tgtEl"), "spTgt").attribute("spid").value();
            const double volume =
                std::clamp(number(node.attribute("vol").value(), 100000) / 100000, 0.0, 1.0);
            const auto timing = child(node, "cTn");
            const bool loop = std::string(timing.attribute("repeatCount").value()) == "indefinite";
            const int slide_count = std::clamp(node.attribute("numSld").as_int(1), 1, 100000);
            for (auto& cue : slide.media_cues)
                if (cue.target == target)
                {
                    cue.volume = node.attribute("mute").as_bool() ? 0 : volume;
                    cue.loop = loop;
                    cue.slide_count = slide_count;
                }
        });
    }

    PresentationAnimationState presentation_animation_state(
        const std::vector<PresentationAnimation>& animations, const std::string& target,
        const std::vector<std::string>& groups, double time, const std::vector<double>& click_times,
        double slide_width, double slide_height,
        const std::map<std::string, std::vector<double>>& trigger_times, int paragraph,
        const mirrorfly::PresentationAnimationTextPosition& text_position)
    {
        PresentationAnimationState state;
        if (!std::isfinite(time) || !std::isfinite(slide_width) || !std::isfinite(slide_height))
            return state;
        bool entrance_seen = false;
        for (const auto& animation : animations)
        {
            if (!std::isfinite(animation.duration) || animation.duration <= 0 ||
                !std::isfinite(animation.repeat) || animation.repeat < 1 || !std::isfinite(animation.delay) ||
                !std::isfinite(animation.rotation) || !std::isfinite(animation.scale[0]) ||
                !std::isfinite(animation.scale[1]))
                continue;
            if (animation.target != target &&
                std::find(groups.begin(), groups.end(), animation.target) == groups.end())
                continue;
            const bool paragraph_range = animation.paragraph_start >= 0 && animation.paragraph_end >= 0;
            const bool character_range = animation.character_start >= 0 && animation.character_end >= 0;
            const bool iterated_text = !animation.iterate_type.empty();
            int iteration = -1;
            int iteration_count = 0;
            if (text_position.character >= 0)
            {
                if (character_range)
                {
                    if (text_position.character < animation.character_start ||
                        text_position.character >= animation.character_end)
                        continue;
                }
                else if (iterated_text)
                {
                    if (animation.iterate_type == "lt")
                    {
                        iteration = text_position.letter;
                        iteration_count = text_position.letter_count;
                    }
                    else if (animation.iterate_type == "wd")
                    {
                        iteration = text_position.word;
                        iteration_count = text_position.word_count;
                    }
                    else
                    {
                        iteration = text_position.element;
                        iteration_count = text_position.element_count;
                    }
                    if (iteration < 0 || iteration_count <= 0)
                        continue;
                    if (animation.iterate_backwards)
                        iteration = iteration_count - iteration - 1;
                }
                else
                    continue;
            }
            else if (character_range || iterated_text)
                continue;
            if ((text_position.character < 0 && paragraph < 0 && paragraph_range) ||
                (paragraph >= 0 &&
                    (!paragraph_range || paragraph < animation.paragraph_start ||
                        paragraph > animation.paragraph_end)))
                continue;
            const bool entrance = animation.category == "entr";
            const bool exit = animation.category == "exit";
            const std::vector<double>* triggered_sequence = &click_times;
            if (!animation.trigger.empty())
            {
                const auto found = trigger_times.find(animation.trigger);
                triggered_sequence = found == trigger_times.end() ? nullptr : &found->second;
            }
            const bool triggered = triggered_sequence && animation.click >= 0 &&
                static_cast<std::size_t>(animation.click) < triggered_sequence->size() &&
                std::isfinite((*triggered_sequence)[animation.click]);
            const double interval = animation.iterate_interval_percent
                ? animation.duration * animation.iterate_interval
                : animation.iterate_interval;
            double elapsed = -1;
            if (triggered)
                elapsed = time - (*triggered_sequence)[animation.click] - animation.delay -
                    std::max(0, iteration) * interval;
            if (elapsed < 0)
            {
                if (entrance && !entrance_seen)
                    state.opacity = 0;
                continue;
            }
            if (entrance)
                entrance_seen = true;
            const auto duration = std::max(0.001, animation.duration);
            const auto total = animation.duration * animation.repeat * (animation.reverse ? 2 : 1);
            const bool ended = elapsed >= total;
            if (ended && !animation.hold && !entrance && !exit)
                continue;
            const auto cycle = duration * (animation.reverse ? 2 : 1);
            const auto position = ended ? (animation.reverse ? 0.0 : duration) : std::fmod(elapsed, cycle);
            const double progress =
                std::clamp(position > duration ? 2 - position / duration : position / duration, 0.0, 1.0);
            const double reveal = exit ? 1 - progress : progress;
            const auto filter = animation.filter.substr(0, animation.filter.find('('));
            if ((paragraph >= 0 || text_position.character >= 0) && (entrance || exit) && filter != "fade" &&
                filter != "appear")
                state.reveal = std::min(state.reveal, reveal);
            if (entrance)
                state.opacity = 1;
            if (exit && ended)
                state.opacity = 0;
            if (filter == "fade")
                state.opacity *= reveal;
            else if (filter == "appear")
            {
                if (exit)
                    state.opacity = 0;
            }
            else if (filter == "fly" || filter == "slide")
            {
                auto direction = animation.direction;
                if (animation.filter.find("Left") != std::string::npos)
                    direction = {-1, 0};
                if (animation.filter.find("Right") != std::string::npos)
                    direction = {1, 0};
                if (animation.filter.find("Top") != std::string::npos)
                    direction = {0, -1};
                if (animation.filter.find("Bottom") != std::string::npos)
                    direction = {0, 1};
                state.x += direction[0] * slide_width * (1 - reveal);
                state.y += direction[1] * slide_height * (1 - reveal);
            }
            else if (filter == "zoom")
                state.scale_x = state.scale_y = reveal;
            else if (filter != "spin" && filter != "grow" && filter != "motion")
            {
                state.clip = animation.filter;
                state.reveal = reveal;
            }
            state.rotation += animation.rotation * progress;
            state.scale_x *= 1 + (animation.scale[0] - 1) * progress;
            state.scale_y *= 1 + (animation.scale[1] - 1) * progress;
            if (!animation.motion.empty())
            {
                const auto point = motion_point(animation.motion, progress);
                state.x += point[0] * slide_width;
                state.y += point[1] * slide_height;
            }
            if (!animation.color.empty() && progress > 0)
                state.color = animation.color;
        }
        return state;
    }
}
