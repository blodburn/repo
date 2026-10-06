#include "ScriptHighlighter.h"

#include <QBrush>
#include <QFont>
#include <QRegularExpression>
#include <QTextDocument>

ScriptHighlighter::ScriptHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    m_dialogueFormat.setFontWeight(QFont::DemiBold);

    m_descriptionFormat.setFontItalic(true);

    m_narrationFormat.setFontUnderline(true);

    m_objectFormat.setFontWeight(QFont::Bold);
    m_objectFormat.setFontUnderline(true);

    m_sceneFormat.setFontWeight(QFont::Bold);

    m_commentFormat.setFontItalic(true);
    m_commentFormat.setForeground(QBrush(Qt::darkGray));
}

void ScriptHighlighter::setObjectLexicon(const QHash<QString, qint64> &lexicon)
{
    m_lexicon = lexicon;
    rehighlight();
}

void ScriptHighlighter::highlightBlock(const QString &text)
{
    const QString trimmed = text.trimmed();

    if (trimmed.startsWith(QStringLiteral("//"))) {
        setFormat(0, text.size(), m_commentFormat);
        return;
    }

    if (trimmed.startsWith(QLatin1Char('#')) &&
        !trimmed.startsWith(QStringLiteral("##"))) {
        setFormat(0, text.size(), m_sceneFormat);
    }

    struct Rule
    {
        QRegularExpression expression;
        QTextCharFormat *format;
    };

    Rule rules[] = {
        {QRegularExpression(QStringLiteral("@@.*?@@")), &m_dialogueFormat},
        {QRegularExpression(QStringLiteral("##.*?##")), &m_descriptionFormat},
        {QRegularExpression(QStringLiteral("₩₩.*?₩₩")), &m_narrationFormat},
        {QRegularExpression(QStringLiteral("\\[\\[.*?\\]\\]")), &m_objectFormat}
    };

    for (const Rule &rule : rules) {
        QRegularExpressionMatchIterator matches = rule.expression.globalMatch(text);

        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), *rule.format);
        }
    }

    for (auto it = m_lexicon.constBegin(); it != m_lexicon.constEnd(); ++it) {
        const QString &name = it.key();
        if (name.isEmpty()) continue;

        int offset = 0;
        while ((offset = text.indexOf(name, offset, Qt::CaseSensitive)) >= 0) {
            setFormat(offset, name.size(), m_objectFormat);
            offset += qMax(1, name.size());
        }
    }
}
