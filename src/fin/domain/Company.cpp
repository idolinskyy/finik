#include "fin/domain/Company.h"

namespace fin {

QString CompanyProfile::heading() const
{
  QString text = name.trimmed();
  if (text.isEmpty()) {
    text = QStringLiteral("Підприємство");
  }
  if (!edrpou.trimmed().isEmpty()) {
    text += QStringLiteral(" (ЄДРПОУ %1)").arg(edrpou.trimmed());
  }
  return text;
}

} // namespace fin
