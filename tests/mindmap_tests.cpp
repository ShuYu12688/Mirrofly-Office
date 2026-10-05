#include <iostream>
#include <limits>
#include <mirrorfly/mindmap.hpp>
#include <string>

namespace
{
    int failures = 0;
    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ++failures;
        }
    }
    bool no_overlap(const mirrorfly::MindMapDocument& document)
    {
        for (std::size_t a = 0; a < document.nodes.size(); ++a)
            for (std::size_t b = a + 1; b < document.nodes.size(); ++b)
            {
                const auto& x = document.nodes[a];
                const auto& y = document.nodes[b];
                if (x.x < y.x + y.width && x.x + x.width > y.x && x.y < y.y + y.height &&
                    x.y + x.height > y.y)
                    return false;
            }
        return true;
    }
    void check_placement_and_limits()
    {
        using namespace mirrorfly;
        auto graph = make_free_mindmap("根 & 中文");
        MindMapCommand command;
        command.type = MindMapCommandType::AddChild;
        command.target_id = "root";
        command.x = 80;
        command.y = 80;
        for (int i = 0; i < 40; ++i)
        {
            command.new_id = "child-" + std::to_string(i);
            expect(apply_mindmap_command(graph, command).changed, "same-origin linked node is created");
        }
        expect(no_overlap(graph) && graph.edges.size() == 40, "siblings repel without breaking connections");
        command = {};
        command.type = MindMapCommandType::MoveNode;
        command.target_id = "child-0";
        command.x = graph.nodes[2].x;
        command.y = graph.nodes[2].y;
        expect(apply_mindmap_command(graph, command).changed && no_overlap(graph),
            "moving into another node settles in free space");
        command.type = MindMapCommandType::StyleNode;
        command.width = 1000;
        command.height = 800;
        expect(apply_mindmap_command(graph, command).changed && no_overlap(graph),
            "resizing also avoids other nodes");
        command.type = MindMapCommandType::MoveNode;
        command.x = 48000;
        command.y = 48000;
        expect(apply_mindmap_command(graph, command).changed && no_overlap(graph),
            "bounded canvas edge supports placement");
        const auto before = serialize_mindmap(graph).text;
        command.x = std::numeric_limits<double>::quiet_NaN();
        expect(apply_mindmap_command(graph, command).error != MindMapError::None &&
                serialize_mindmap(graph).text == before,
            "invalid moves are atomic");
        expect(parse_mindmap(before).document.nodes.size() == graph.nodes.size(),
            "positions and Unicode roundtrip");
        expect(!is_mindmap_path("old.MM") && is_mindmap_path("新文件.MFG") && !is_mindmap_path("map.mfg.zip"),
            "only exact MFG extensions are accepted");
        expect(parse_mindmap("<map version='1.0.1'><node ID='root' TEXT='old'/></map>").error ==
                MindMapError::Unsupported,
            "legacy XML renamed to MFG is still rejected");
        expect(parse_mindmap("<!DOCTYPE map [<!ENTITY e SYSTEM 'file:///secret'>]><map/>").error ==
                MindMapError::InvalidXml,
            "external entity declarations are rejected");
        expect(parse_mindmap(std::string(maximum_mindmap_xml_bytes + 1, ' ')).error == MindMapError::TooLarge,
            "XML budget precedes parsing");
        expect(parse_mindmap(before + "<extra/>").error != MindMapError::None, "multiple roots are rejected");
        auto invalid = graph;
        invalid.nodes.back().id = "root";
        expect(validate_mindmap(invalid).error != MindMapError::None, "duplicate identifiers are rejected");
        invalid = graph;
        invalid.nodes[0].text.assign(maximum_mindmap_node_text_bytes + 1, 'x');
        expect(serialize_mindmap(invalid).error != MindMapError::None, "text budget applies on output");
    }

    void check_branch_layout()
    {
        using namespace mirrorfly;
        auto graph = make_free_mindmap();
        // Three uneven branches, including wide/tall nodes; no useful initial coordinates.
        for (int branch = 0; branch < 3; ++branch)
        {
            const auto parent = "branch-" + std::to_string(branch);
            MindMapNode node;
            node.id = parent;
            node.width = 300 + branch * 40;
            graph.nodes.push_back(node);
            graph.edges.push_back({"edge-" + parent, "root", parent, "branch"});
            for (int leaf = 0; leaf <= branch * 2; ++leaf)
            {
                node.id = parent + "-" + std::to_string(leaf);
                node.height = 70 + leaf * 35;
                graph.nodes.push_back(node);
                graph.edges.push_back({"edge-" + node.id, parent, node.id, "leaf"});
            }
        }
        MindMapNode detached;
        detached.id = "detached";
        graph.nodes.push_back(detached);
        MindMapCommand command;
        command.type = MindMapCommandType::AutoLayout;
        expect(apply_mindmap_command(graph, command).changed && no_overlap(graph),
            "whole branches reserve height and columns without node overlap");
        const auto arranged = serialize_mindmap(graph).text;
        expect(!apply_mindmap_command(graph, command).changed && serialize_mindmap(graph).text == arranged,
            "layout is deterministic and idempotent");
        expect(serialize_mindmap(parse_mindmap(arranged).document).text == arranged,
            "layout positions, labels and styles survive serialization");
        graph.edges.push_back({"cross", graph.nodes[2].id, graph.nodes[4].id, "reference"});
        graph.edges.push_back({"cycle", graph.nodes[4].id, "root", "return"});
        graph.edges.push_back({"self", "root", "root", "loop"});
        const auto edges = graph.edges.size();
        expect(apply_mindmap_command(graph, command).error == MindMapError::None && no_overlap(graph) &&
                graph.edges.size() == edges && graph.nodes.back().id == "detached",
            "cycles, cross-links, isolated components and identities are preserved");
        graph.read_only = true;
        expect(apply_mindmap_command(graph, command).error == MindMapError::ReadOnly,
            "layout respects read-only boundary");
        graph = make_free_mindmap();
        for (int i = 0; i < 80; ++i)
        {
            MindMapNode node;
            node.id = "tall-" + std::to_string(i);
            node.height = 800;
            graph.nodes.push_back(node);
            graph.edges.push_back({"edge-" + node.id, "root", node.id, {}});
        }
        const auto before = serialize_mindmap(graph).text;
        expect(apply_mindmap_command(graph, command).error == MindMapError::TooLarge &&
                serialize_mindmap(graph).text == before,
            "oversized layout fails atomically without partially moving nodes");
    }
}

int run_mindmap_tests()
{
    using namespace mirrorfly;
    auto graph = make_free_mindmap();
    MindMapCommand create;
    create.type = MindMapCommandType::CreateNode;
    create.new_id = "independent";
    create.text = "独立节点";
    create.x = 420;
    create.y = 280;
    expect(apply_mindmap_command(graph, create).changed && graph.edges.empty(),
        "independent node is not silently parented");
    MindMapCommand link;
    link.type = MindMapCommandType::Connect;
    link.target_id = "root";
    link.parent_id = "independent";
    link.new_id = "forward";
    expect(apply_mindmap_command(graph, link).changed, "directed graph link");
    link.target_id = "independent";
    link.parent_id = "root";
    link.new_id = "return";
    expect(apply_mindmap_command(graph, link).changed, "cycles are valid in the free graph model");
    link.new_id = "duplicate";
    expect(apply_mindmap_command(graph, link).error != MindMapError::None && graph.edges.size() == 2,
        "duplicate connection fails atomically");
    MindMapCommand style;
    style.type = MindMapCommandType::StyleNode;
    style.target_id = "independent";
    style.shape = "diamond";
    style.border = "#326976";
    style.fill = "#F2FAFB";
    style.width = 260;
    style.border_width = 3;
    expect(apply_mindmap_command(graph, style).changed, "custom node style");
    const auto xml = serialize_mindmap(graph);
    const auto parsed = parse_mindmap(xml.text);
    expect(xml.error == MindMapError::None && parsed.error == MindMapError::None &&
            parsed.document.free_layout && parsed.document.edges.size() == 2 &&
            parsed.document.nodes[1].shape == "diamond" && parsed.document.nodes[1].x == 420,
        "positions, style and cycles survive graph serialization");
    expect(is_mindmap_path("flow.MFG"), "free graph path recognized");
    const auto original = graph.nodes[1].x;
    MindMapCommand move;
    move.type = MindMapCommandType::MoveNode;
    move.target_id = "independent";
    move.x = -1;
    expect(apply_mindmap_command(graph, move).error != MindMapError::None && graph.nodes[1].x == original,
        "invalid geometry preserves previous graph");
    check_placement_and_limits();
    check_branch_layout();
    return failures == 0 ? 0 : 1;
}

int main()
{
    return run_mindmap_tests();
}
