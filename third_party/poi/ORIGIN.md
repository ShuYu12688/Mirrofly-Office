# Apache POI DrawingML preset data

- Upstream: https://poi.apache.org/ and https://github.com/apache/poi
- Version: 5.4.1 (`REL_5_4_1`), Maven artifact `org.apache.poi:poi:5.4.1`.
- Only `org/apache/poi/sl/draw/geom/presetShapeDefinitions.xml` is used, unmodified. No Java runtime or POI bytecode is included in the application.
- Original artifact: https://repo.maven.apache.org/maven2/org/apache/poi/poi/5.4.1/poi-5.4.1.jar
- Retrieved from Huawei Cloud Maven mirror on 2026-09-17 after the upstream TLS connection failed. Artifact SHA-256 matches the mirror's published `.sha256`: `DA5ABF42DA4604C5A7BCA38956AF6E9D6F196D9B6D4CB7EABEE4F480B580D505`.
- XML SHA-256: `A7DAD593D27BD70536B41DA9B761FA16409536CC0C25EF2B6C7A61C5D9B3E738`.
- The artifact's original `LICENSE` and `NOTICE` are retained. The preset file contains 187 geometries. The guide evaluator and rendering adapter in this project are separate C++ implementations.

CMake embeds this fixed local data at build time; normal builds do not download it. Both Qt and Qt-free builds use the same data.
