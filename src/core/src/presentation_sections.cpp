#include "presentation_sections.hpp"

#include <utf8.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace
{
    using Node = pugi::xml_node;
    using Action = mirrorfly::PresentationEditAction;
    constexpr const char* section_extension = "{521415D9-36F7-43E2-AB2F-B90AF26B5E84}";
    constexpr const char* section_namespace = "http://schemas.microsoft.com/office/powerpoint/2010/main";

    std::string local(const char* name)
    {
        const auto separator = std::strchr(name, ':');
        return separator ? separator + 1 : name;
    }

    Node child(Node parent, const char* name)
    {
        for (auto item : parent.children())
            if (local(item.name()) == name)
                return item;
        return {};
    }

    Node section_extension_node(Node presentation)
    {
        for (auto extension : child(presentation, "extLst").children())
            if (local(extension.name()) == "ext" && child(extension, "sectionLst"))
                return extension;
        return {};
    }

    std::string new_section_id()
    {
        std::random_device random;
        std::array<unsigned char, 16> bytes{};
        for (auto& byte : bytes)
            byte = static_cast<unsigned char>(random());
        bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
        bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
        char output[39]{};
        std::snprintf(output, sizeof(output),
            "{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}", bytes[0], bytes[1],
            bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
            bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        return output;
    }

    bool valid_name(const std::string& name)
    {
        if (name.empty() || name.size() > 128 || !utf8::is_valid(name.begin(), name.end()))
            return false;
        for (const unsigned char character : name)
            if (character < 32 && character != '\t' && character != '\n' && character != '\r')
                return false;
        return true;
    }

    mirrorfly::PresentationEditResult failure(mirrorfly::PresentationEditError error, std::string message)
    {
        mirrorfly::PresentationEditResult result;
        result.error = error;
        result.message = std::move(message);
        return result;
    }

    std::size_t section_position(const mirrorfly::PresentationScene& scene, const std::string& id)
    {
        for (std::size_t index = 0; index < scene.sections.size(); ++index)
            if (scene.sections[index].id == id)
                return index;
        return scene.sections.size();
    }
}

namespace mirrorfly
{
    std::vector<std::string> read_presentation_sections(
        Node presentation, const std::vector<std::string>& slide_ids, PresentationScene& scene)
    {
        std::vector<std::string> membership(slide_ids.size());
        int section_lists = 0;
        for (auto item : child(presentation, "extLst").children())
            if (local(item.name()) == "ext" && child(item, "sectionLst"))
                ++section_lists;
        if (section_lists > 1)
        {
            scene.section_structure_locked = true;
            return membership;
        }
        const auto extension = section_extension_node(presentation);
        if (!extension)
            return membership;
        const auto list = child(extension, "sectionLst");
        std::size_t slide = 0;
        std::set<std::string> used_ids;
        std::vector<PresentationSection> sections;
        bool valid = std::string(extension.attribute("uri").value()) == section_extension;
        for (auto section : list.children())
        {
            if (local(section.name()) != "section")
            {
                valid = false;
                break;
            }
            const std::string id = section.attribute("id").value();
            const std::string name = section.attribute("name").value();
            const auto ids = child(section, "sldIdLst");
            if (id.empty() || id.size() > 128 || name.size() > 256 || !used_ids.insert(id).second || !ids ||
                sections.size() >= slide_ids.size())
            {
                valid = false;
                break;
            }
            const auto begin = slide;
            for (auto entry : ids.children())
            {
                if (local(entry.name()) != "sldId")
                {
                    valid = false;
                    break;
                }
                if (slide >= slide_ids.size() || slide_ids[slide] != entry.attribute("id").value())
                {
                    valid = false;
                    break;
                }
                membership[slide++] = id;
            }
            if (!valid || slide == begin)
            {
                valid = false;
                break;
            }
            sections.push_back({id, name});
        }
        if (!valid || sections.empty() || slide != slide_ids.size())
        {
            scene.section_structure_locked = true;
            return std::vector<std::string>(slide_ids.size());
        }
        scene.sections = std::move(sections);
        return membership;
    }

    void patch_presentation_sections(
        Node presentation, const PresentationScene& scene, const std::vector<std::string>& slide_ids)
    {
        if (scene.section_structure_locked)
            throw std::runtime_error("当前文件的节结构无法安全编辑，原始内容已保留。");
        if (slide_ids.size() != scene.slides.size())
            throw std::runtime_error("节的页面 ID 数量不一致。");
        auto extension = section_extension_node(presentation);
        auto old_list = child(extension, "sectionLst");
        if (scene.sections.empty())
        {
            if (old_list)
            {
                extension.remove_child(old_list);
                if (!extension.first_child())
                {
                    auto list = extension.parent();
                    list.remove_child(extension);
                    if (!list.first_child())
                        presentation.remove_child(list);
                }
            }
            return;
        }
        if (scene.sections.size() > slide_ids.size() || scene.sections.size() > 200)
            throw std::runtime_error("节的数量超过页面数量。");
        std::map<std::string, std::vector<std::string>> members;
        std::size_t next_section = 0;
        for (std::size_t index = 0; index < scene.slides.size(); ++index)
        {
            const auto& id = scene.slides[index].section_id;
            if (next_section >= scene.sections.size() || id != scene.sections[next_section].id)
            {
                if (next_section + 1 >= scene.sections.size() || id != scene.sections[next_section + 1].id)
                    throw std::runtime_error("节的页面必须连续且与节顺序一致。");
                ++next_section;
            }
            members[id].push_back(slide_ids[index]);
        }
        if (next_section + 1 != scene.sections.size())
            throw std::runtime_error("节不能没有页面。");
        const auto existing_namespace = presentation.attribute("xmlns:p14");
        if (existing_namespace && std::string(existing_namespace.value()) != section_namespace)
            throw std::runtime_error("p14 命名空间与节结构不一致。");
        if (!existing_namespace)
            presentation.append_attribute("xmlns:p14") = section_namespace;
        if (!extension)
        {
            auto list = child(presentation, "extLst");
            if (!list)
                list = presentation.append_child("p:extLst");
            extension = list.append_child("p:ext");
            extension.append_attribute("uri") = section_extension;
        }
        pugi::xml_document temporary;
        auto replacement = temporary.append_child("p14:sectionLst");
        std::map<std::string, Node> existing;
        for (auto item : old_list.children())
            if (local(item.name()) == "section")
                existing[item.attribute("id").value()] = item;
        for (const auto& section : scene.sections)
        {
            if (section.id.empty() || section.name.size() > 256)
                throw std::runtime_error("节名称或标识无效。");
            Node item;
            if (const auto found = existing.find(section.id); found != existing.end())
                item = replacement.append_copy(found->second);
            else
                item = replacement.append_child("p14:section");
            auto id = item.attribute("id");
            if (!id)
                id = item.append_attribute("id");
            id = section.id.c_str();
            auto name = item.attribute("name");
            if (!name)
                name = item.append_attribute("name");
            name = section.name.c_str();
            auto ids = child(item, "sldIdLst");
            if (!ids)
                ids = item.prepend_child("p14:sldIdLst");
            ids.remove_children();
            for (const auto& slide_id : members.at(section.id))
                ids.append_child("p14:sldId").append_attribute("id") = slide_id.c_str();
        }
        if (old_list)
        {
            extension.insert_copy_before(replacement, old_list);
            extension.remove_child(old_list);
        }
        else
            extension.append_copy(replacement);
    }

    PresentationEditResult edit_presentation_section(
        PresentationScene& scene, const PresentationEditCommand& command, PresentationEditResult result)
    {
        if (scene.section_structure_locked)
            return failure(PresentationEditError::ReadOnly, "当前演示文稿的节结构无法安全编辑。");
        if (command.slide_index >= scene.slides.size())
            return failure(PresentationEditError::InvalidIndex, "节所在页面无效。");
        const auto current_id = scene.slides[command.slide_index].section_id;
        const auto position = section_position(scene, current_id);
        if (command.action == Action::CreateSection)
        {
            if (!valid_name(command.section_name) || scene.sections.size() >= 200)
                return failure(PresentationEditError::InvalidValue, "节名称无效或节数量已达上限。");
            if (scene.sections.empty())
            {
                if (command.slide_index > 0)
                {
                    const auto first = new_section_id();
                    scene.sections.push_back({first, "默认节"});
                    for (std::size_t index = 0; index < command.slide_index; ++index)
                        scene.slides[index].section_id = first;
                }
                const auto id = new_section_id();
                scene.sections.push_back({id, command.section_name});
                for (std::size_t index = command.slide_index; index < scene.slides.size(); ++index)
                    scene.slides[index].section_id = id;
                return result;
            }
            if (position >= scene.sections.size() || command.slide_index == 0 ||
                scene.slides[command.slide_index - 1].section_id != current_id)
                return failure(PresentationEditError::InvalidValue, "当前页已位于节起点，请使用重命名节。");
            const auto id = new_section_id();
            scene.sections.insert(scene.sections.begin() + static_cast<std::ptrdiff_t>(position + 1),
                {id, command.section_name});
            for (std::size_t index = command.slide_index;
                index < scene.slides.size() && scene.slides[index].section_id == current_id; ++index)
                scene.slides[index].section_id = id;
            return result;
        }
        if (position >= scene.sections.size() || current_id.empty())
            return failure(PresentationEditError::InvalidValue, "当前页没有可编辑的节。");
        if (command.action == Action::RenameSection)
        {
            if (!valid_name(command.section_name))
                return failure(PresentationEditError::InvalidValue, "节名称无效。");
            scene.sections[position].name = command.section_name;
            return result;
        }
        if (command.action == Action::RemoveSection)
        {
            std::string target;
            if (scene.sections.size() > 1)
                target = position > 0 ? scene.sections[position - 1].id : scene.sections[position + 1].id;
            for (auto& slide : scene.slides)
                if (slide.section_id == current_id)
                    slide.section_id = target;
            scene.sections.erase(scene.sections.begin() + static_cast<std::ptrdiff_t>(position));
            return result;
        }
        return failure(PresentationEditError::InvalidValue, "未知的节操作。");
    }

    void assign_inserted_slide_section(PresentationScene& scene, std::size_t slide_index)
    {
        if (scene.sections.empty())
            return;
        auto& inserted = scene.slides[slide_index];
        inserted.section_id = slide_index + 1 < scene.slides.size()
            ? scene.slides[slide_index + 1].section_id
            : scene.slides[slide_index - 1].section_id;
    }

    void reconcile_deleted_slide_sections(PresentationScene& scene)
    {
        const auto empty = [&](const auto& section)
        {
            return std::none_of(scene.slides.begin(), scene.slides.end(), [&](const auto& slide)
            {
                return slide.section_id == section.id;
            });
        };
        scene.sections.erase(
            std::remove_if(scene.sections.begin(), scene.sections.end(), empty), scene.sections.end());
    }

    void reconcile_moved_slide_section(PresentationScene& scene, std::size_t source, std::size_t target)
    {
        if (scene.sections.empty() || scene.slides[source].section_id == scene.slides[target].section_id)
            return;
        scene.slides[target].section_id = scene.slides[source].section_id;
        reconcile_deleted_slide_sections(scene);
    }
}
