#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QString>
#include <QVariantMap>

namespace mirrorfly
{

    class EditorTools final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool canSave READ canSave NOTIFY saveStateChanged)

    public:
        explicit EditorTools(QObject* parent = nullptr);
        ~EditorTools() override;

        Q_INVOKABLE QVariantMap loadDocument(QQuickTextDocument* document, const QString& source,
            bool markdown, const QVariantMap& theme, const QString& documentPath = {});
        Q_INVOKABLE QVariantMap inspectDocument(QQuickTextDocument* document, int position) const;

        bool canSave() const;
        Q_INVOKABLE QString sourceText(QQuickTextDocument* document);

        // Input and returned document positions use QString / TextArea UTF-16 offsets.
        Q_INVOKABLE QVariantMap applyEdit(QQuickTextDocument* document, int selectionStart, int selectionEnd,
            const QString& action, const QVariantMap& options = {});

    signals:
        void documentEdited(QQuickTextDocument* document);
        void saveStateChanged();

    private:
        QPointer<QTextDocument> editor_document_;
        QPointer<QQuickTextDocument> wrapper_;
        QVariantMap theme_;
        QString original_source_;
        QString baseline_markdown_;
        QString last_valid_source_;
        QString serialization_error_;
        bool markdown_ = false;
        bool loading_ = false;
        bool managed_edit_ = false;
        bool editable_ = true;
        bool can_save_ = true;
    };

}
