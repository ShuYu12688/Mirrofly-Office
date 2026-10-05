#include "editor_tools.hpp"
#include "markdown_document.hpp"

#include <QScopedValueRollback>
#include <QTextDocument>
#include <QUrl>

namespace mirrorfly
{

    EditorTools::EditorTools(QObject* parent) : QObject(parent)
    {
    }

    EditorTools::~EditorTools()
    {
        if (editor_document_)
        {
            disconnect(editor_document_, nullptr, this, nullptr);
        }
    }

    bool EditorTools::canSave() const
    {
        return can_save_;
    }

    QVariantMap EditorTools::loadDocument(QQuickTextDocument* document, const QString& source, bool markdown,
        const QVariantMap& theme, const QString& documentPath)
    {
        if (document == nullptr || loading_ || managed_edit_)
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("编辑区尚未准备好。")}};
        }

        QScopedValueRollback<bool> loading(loading_, true);
        // Changing TextEdit's format can rewrite its old document; keep that inside the load transaction.
        auto* text_control = document->parent();
        if (!text_control ||
            !text_control->setProperty("textFormat", markdown ? Qt::RichText : Qt::PlainText))
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("编辑区不支持当前文档格式。")}};
        }
        auto next = create_editor_document();
        if (!documentPath.isEmpty())
            next->setBaseUrl(QUrl::fromLocalFile(documentPath));
        QString error = markdown ? markdown_support_error(source) : QString{};
        MarkdownSerialization baseline;
        if (markdown && error.isEmpty())
        {
            error = load_markdown_document(*next, source, theme);
            if (error.isEmpty())
            {
                baseline = serialize_markdown_document(*next);
                if (!baseline.valid)
                    error = baseline.error;
            }
        }
        if (!markdown || !error.isEmpty())
        {
            next->setPlainText(error.isEmpty()
                    ? source
                    : QStringLiteral("这份文档暂时无法进入可视编辑。\n原始内容和文件均已保留。"));
        }
        next->setUndoRedoEnabled(true);
        next->clearUndoRedoStacks();
        next->setModified(false);
        auto* previous_document = editor_document_.data();
        QPointer<QTextDocument> replaced_document = document->textDocument();
        if (previous_document)
        {
            disconnect(previous_document, nullptr, this, nullptr);
        }
        QPointer<QQuickTextDocument> target_wrapper = document;
        wrapper_ = target_wrapper;
        theme_ = theme;
        original_source_ = source;
        last_valid_source_ = source;
        markdown_ = markdown;
        editable_ = error.isEmpty();
        serialization_error_ = error;
        can_save_ = true;
        next->setParent(document);
        QPointer<QTextDocument> installed_document = next.release();
        document->setTextDocument(installed_document);
        editor_document_ = installed_document;
        if (replaced_document && target_wrapper && replaced_document->parent() == target_wrapper)
        {
            delete replaced_document.data();
        }
        if (!target_wrapper || !installed_document)
        {
            wrapper_.clear();
            editor_document_.clear();
            editable_ = false;
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("编辑区已关闭，文档没有载入。")}};
        }
        baseline_markdown_ = baseline.source;
        connect(editor_document_, &QTextDocument::contentsChanged, this, [this]()
        {
            if (!loading_ && editable_ && wrapper_)
            {
                if (managed_edit_)
                {
                    return;
                }
                emit documentEdited(wrapper_);
            }
        });
        emit saveStateChanged();
        return {{QStringLiteral("valid"), editable_}, {QStringLiteral("error"), error}};
    }

    QVariantMap EditorTools::inspectDocument(QQuickTextDocument* document, int position) const
    {
        if (!document || !document->textDocument())
        {
            return {};
        }
        auto state = inspect_markdown_document(*document->textDocument(), position);
        state.insert(QStringLiteral("error"), document == wrapper_ ? serialization_error_ : QString{});
        state.insert(QStringLiteral("canSave"), can_save_);
        return state;
    }

    QString EditorTools::sourceText(QQuickTextDocument* document)
    {
        if (document == nullptr || document->textDocument() == nullptr)
        {
            return {};
        }

        if (document == wrapper_ && markdown_)
        {
            if (!editable_)
            {
                return original_source_;
            }
            const auto serialized = serialize_markdown_document(*document->textDocument());
            const bool changed = can_save_ != serialized.valid;
            can_save_ = serialized.valid;
            serialization_error_ = serialized.error;
            if (serialized.valid)
            {
                last_valid_source_ =
                    serialized.source == baseline_markdown_ ? original_source_ : serialized.source;
            }
            if (changed)
            {
                emit saveStateChanged();
            }
            return last_valid_source_;
        }

        auto source = document->textDocument()->toRawText();
        source.replace(QChar::ParagraphSeparator, QChar(u'\n'));
        return source;
    }

    QVariantMap EditorTools::applyEdit(QQuickTextDocument* document, int selectionStart, int selectionEnd,
        const QString& action, const QVariantMap& options)
    {
        if (!document || !document->textDocument() || document != wrapper_ || !markdown_ || !editable_ ||
            loading_)
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("当前文档不能执行这项排版操作。")}};
        }
        if (managed_edit_)
        {
            return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("当前排版操作尚未完成，请稍后再试。")}};
        }
        QVariantMap result;
        {
            QScopedValueRollback<bool> managed_edit(managed_edit_, true);
            result = edit_markdown_document(
                *document->textDocument(), selectionStart, selectionEnd, action, options, theme_);
        }
        const bool handled = result.value(QStringLiteral("handled"), true).toBool();
        if (result.value(QStringLiteral("valid")).toBool() && handled && wrapper_ == document)
        {
            emit documentEdited(document);
        }
        return result;
    }

}
