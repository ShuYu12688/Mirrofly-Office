#pragma once

#include <pugixml.hpp>

#include <string>

namespace mirrorfly
{
    // Reads only the notes body placeholder; slide numbers and date fields are excluded.
    std::string read_presentation_speaker_notes(pugi::xml_node notes);
}
