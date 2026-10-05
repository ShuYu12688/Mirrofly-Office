#include "office_ai_tools.hpp"

namespace mirrorfly
{
    QJsonObject office_ai_guide(const QString& group)
    {
        static const QJsonObject guides{
            {"compose",
                "已加载当前页批量生成工具；新建Word用office_compose_word，表格用office_compose_table，"
                "PPT追加页面用office_compose_slides。只用本轮列出的对应工具；局部修改用office_"
                "read与精确编辑。"},
            {"slides",
                "已有页先office_read(content,index=从0开始页码)取对象ID；text须提供该ID。"
                "format+id可读对象绘制格式及首个文字片段，不代表整框格式一致。字号/字体/颜色"
                "查office_schema(slides,formatText)，用office_batch依次selectObject和applyEdit。"
                "导入只读先office_editable_copy，指定路径用destination；编辑后office_save(current=true)。"
                "坐标单位pt。formatShape支持16种常用fillPattern、独立patternForegroundColor/"
                "patternBackgroundColor和11种lineDash；枚举查schema。线型需非空outlineColor且"
                "outlineWidth>0才可见，只设置lineDash不启用描边。图案纹理采用近似预览，"
                "patternFillSupported=false表示原图案仅显示背景，保留原包，不承诺像素兼容；"
                "改文字/轮廓不替换未知填充，明确改填充才替换。批量新增先office_load_group(compose)，"
                "配图按需加载media。"},
            {"media",
                "office_image_search 搜索开放授权图片；office_image_fetch(id)下载已返回的候选。使用其 "
                "imagePath 与授权信息。失败可不用图片，不猜ID或路径。"},
            {"word",
                "已有文件需修改时，检查点若readOnly=true，先office_editable_copy；指定路径用"
                "destination，随后在副本中定位，避免复制前后重复读取。office_read(content)按页列段落"
                "和UTF-16 start/end（end不含）；textComplete=true表示预览已是完整段落，无需再读text。"
                "format+index=段落号读取位置及段落格式，同时给段落范围和预览；offset为段内UTF-16"
                "位置，格式不代表整段一致。更长文字用text+index及nextOffset。"
                "格式操作查office_schema(word,具体格式名，如size)，返回已含word.format调用参数，"
                "不必再查format。单/双下划线或删除线用underlineStyle/strikeStyle，值none/single/double；"
                "旧underline/strike布尔值true设单线，false取消。已知目标后，独立的读取与schema可同轮调用。"
                "常用项目符号及数字/字母/罗马编号查listMarker；选中段落成为独立列表。"
                "listStart设置选区独立起始编号，需选中同一个编号列表的段落，不自动包含后续未选段落；"
                "若要整段续排，范围须覆盖后续目标段落。format中的listStyle回读样式、起点和当前标记；"
                "displaySupported=false表示原编号尚不能完整显示，保留原定义，不宣称视觉已完整兼容。"
                "制表位用tabStops，先读format.tabStops，传完整有序数组替换；[]清除自定义及继承制表位。"
                "position单位pt，对齐/前导线取schema枚举；不要用空格或点号替换正文Tab。"
                "tabLayoutSupported=false表示首行/悬挂缩进或段内软换行与居中/右对齐制表位组合的"
                "预览及PDF排版受限，tabLayoutReason说明边界；DOCX保留位置与对齐数据；"
                "原位置仍可读、编辑和保存，不能据此宣称组合视觉已完整兼容。"
                "word.insertText(start,end,text)改文本；word.insertParagraph(start,end)分段；"
                "tableGap=true为空缺不可写。修改后核验受影响范围；仅改字号无需反复读取所有相邻段落。"
                "office_save(current=true)保存副本，file.saved=true且path正确即落盘，不必再查状态。"
                "新建批量内容按需office_load_group(compose)。"},
            {"sheets",
                "批量生成按需office_load_group(compose)。已有文件先"
                "office_read(overview)列工作表；content+index列已存单元格；text/formula+id=A1读取全文。"
                "检查样式用office_read(format,id=A1)，format含表样式，display另含条件颜色；仅改指定字段。"
                "报告旧值须引用同一单元格的修改前读取或observedChanges的before；after是修改后值，"
                "未留存的读数先重读，不能猜；核验只读，不恢复旧值再重做；不能由format推断直接填充来源。"
                "shrinkToFit仅缩小单行显示，不改字号；wrap或多行文本不缩小。"
                "文字方向使用formatSelection的textRotation，角度编码查schema；不修改正文来模拟竖排。"
                "行列从0开始。批量写：sheets.selectAddress('A1')，sheets.pasteText(制表符分列、换行分行)。"
                "公式保留等号；格式/尺寸参数查schema。结构操作走 sheets.startTool(action,args)，"
                "先读取当前工作表和选区，参数查 office_schema(sheets,startTool)；删除前确认用户目标。"},
            {"mindmap",
                "用 office_read(module=mindmap,view=overview) 取得 rootId/selectedId；"
                "office_read 的 content 和 edges 分页读图，勿把 view 传给 mindmap.readContent。"
                "office_action 调 mindmap.execute 时，args 是有序参数数组，例如"
                "[\"addChild\",{\"id\":\"root\",\"newId\":\"b1\",\"text\":\"基础语法\"}]；"
                "office_batch.steps 的每个 mindmap.execute 也用这个数组，不要在 args 里再嵌套"
                " action/args 对象。"
                "保留根节点；改名调用 [\"rename\",{\"id\":\"root\",\"text\":\"主题\"}]，"
                "动作名是 rename，不是 renameNode。addChild指定父id及唯一newId，后续同批可引用newId。"
                "新增全图后用 [\"autoLayout\",{}] 一次，无需逐节点猜坐标；"
                "保存新文件用 office_save 的 title，不把 title 传给 mindmap.saveTo；"
                "修改用户布局时保留未要求改变的部分。"},
            {"text",
                "新建writer或markdown，模块均为text。局部样式用text.formatMarkdown；首次调用前查"
                "office_schema(module=text,name=formatMarkdown)"
                "，完整动作、参数和边界在schema中，已加载时直接使用。"
                "先office_read(module=text,view=find,text=精确原文)取得源UTF-16的start/end，按context选目标，"
                "传options.expectedText为匹配原文。禁止手算字符数；find大小写敏感，nextOffset为源位置。"
                "每个调用都传options对象。光标插入用start=end=find.start，expectedText只校验后文。"
                "每次只编辑一个目标，完成后再find下一个未完成目标；不要并行提交依赖旧偏移的操作。"
                "成功回执和recentAcceptedEdits表示操作已经执行，位置变化不代表需要重做。"
                "listIndent/listOutdent是相对移动一层，已经成功就不能重复；先核对当前层级与已接受回执。"
                "部分调用被not_executed_"
                "reobserve跳过时，只重查并执行跳过的目标。全部目标已满足就保存，勿重复格式动作。"
                "核验用office_read(text)"
                "读取当前源，按nextOffset分页。replaceContent替换全文，必须先读全，不能用预览覆盖。"
                "保留其余字符、空白、链接、图片和引用/列表父子结构；源标记/数字实体不同于渲染文字。"
                "HTML/脚注符号在代码内为字面量；实际HTML、脚注定义和前置元数据保护原文并暂停可视编辑。"
                "已有文件/"
                "副本用office_save(current=true)，只有需要新副本时才另存；确认file.saved与path后结束。"},
            {"pdf",
                "office_open已有PDF；overview列所有页ID，text+index读页文字，annotations读批注。"
                "pdf.selectPage切页，pdf.execute(action,args)"
                "编辑，参数查schema；批注位置为旋转后页面归一化坐标。"
                "office_action示例：op=pdf.execute,args=[\"rotate\",{\"pageId\":\"实际ID\",\"turns\":1}]。"
                "成功回执表示编辑已应用，旋转是增量操作，不重复执行；overview的rotation是当前角度。"
                "office_save保存副本。sourceTruncated表示原始提取有上限，扫描图片没有OCR。"},
            {"export",
                "export.start(module,destination,options)异步导出PDF，选项见semantics；仅用户要求时导出。"
                "结束必须snapshot.success=true与path；失败不重复start。"},
            {"images",
                "images.start(parentFolderUrl,options)异步导出PPT到新图片文件夹；选项见semantics。"
                "结束必须snapshot.success=true与path。"}};
        if (!guides.contains(group))
            return {{"ok", false}, {"error", "unknown_group"}};
        return {{"ok", true}, {"guide", guides.value(group)}};
    }
}
