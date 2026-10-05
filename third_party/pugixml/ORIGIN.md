# pugixml origin

- Upstream: https://github.com/zeux/pugixml
- Release: 1.15
- Git commit: ee86beb30e4973f5feffe3ce63bfa4fbadf72f38
- Retrieved: 2026-09-13
- License: MIT, see LICENSE.md.

The four upstream source/license files are unmodified. Only pugixml.cpp is compiled, privately linked to mirrorfly_core with PUGIXML_NO_XPATH. No XML parser types are exposed in public headers. XML is parsed from bounded package memory; Mirrorfly rejects DOCTYPE and limits total XML nodes and nesting. No external document/entity resolution is provided.

Tag verified through https://api.github.com/repos/zeux/pugixml/git/ref/tags/v1.15. Raw files were fetched from the fixed commit; no upstream scripts were executed.

| File | SHA-256 |
| --- | --- |
| LICENSE.md | 0d0b3772af2fa45628a548a3b34583707ebcc68bb6f83ec48ca273aab4a510f1 |
| pugiconfig.hpp | 981cd9ad3313878817d7b548c9929cc09d3bc42be2c0c1dcb0d77290fc98b92d |
| pugixml.cpp | 67c3892efba51d4e4eb6ce5609286fd2703aede6ba49a6d2130068cad09e0744 |
| pugixml.hpp | 2555f950fd080e02ff16f36698ddf8d4a46d3a484b9337b4cbfc6300043be734 |
