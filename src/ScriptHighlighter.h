#pragma once

#include <QHash>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextDocument;

class ScriptHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit ScriptHighlighter(QTextDocument *parent = nullptr);

    void setObjectLexicon(const QHash<QString, qint64> &lexicon);

protected:
    void highlightBlock(const QString &text) override;

private:
    QHash<QString, qint64> m_lexicon;

    QTextCharFormat m_dialogueFormat;
    QTextCharFormat m_descriptionFormat;
    QTextCharFormat m_narrationFormat;
    QTextCharFormat m_objectFormat;
    QTextCharFormat m_sceneFormat;
    QTextCharFormat m_commentFormat;
};
