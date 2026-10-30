/**
 * @file pending_selection.cpp
 * @brief The selection value's QML spelling.
 */

#include "pending_selection.h"

namespace lens::app {

QString kindName(core::SelectionKind const kind)
{
    switch (kind) {
        case core::SelectionKind::Entity: return QStringLiteral("entity");
        case core::SelectionKind::Sentence: return QStringLiteral("sentence");
        case core::SelectionKind::Translation: return QStringLiteral("translation");
        case core::SelectionKind::Word: return QStringLiteral("word");
    }
    return QStringLiteral("word");
}

}
