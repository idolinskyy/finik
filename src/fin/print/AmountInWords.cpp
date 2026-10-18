#include "fin/print/AmountInWords.h"

#include <QStringList>

namespace fin {
namespace {

enum class Gender
{
  Masculine,
  Feminine
};

const char * unitWord(int digit, Gender gender)
{
  static const char * masculine[] = {"",      "один",  "два", "три",   "чотири",
                                     "п'ять", "шість", "сім", "вісім", "дев'ять"};
  static const char * feminine[] = {"",      "одна",  "дві", "три",   "чотири",
                                    "п'ять", "шість", "сім", "вісім", "дев'ять"};
  if (digit < 0 || digit > 9) {
    return "";
  }
  return gender == Gender::Feminine ? feminine[digit] : masculine[digit];
}

QString triadToWords(int value, Gender gender)
{
  if (value <= 0) {
    return {};
  }
  static const char * hundreds[] = {"",        "сто",     "двісті", "триста",   "чотириста",
                                    "п'ятсот", "шістсот", "сімсот", "вісімсот", "дев'ятсот"};
  static const char * tens[] = {"",          "",          "двадцять", "тридцять",   "сорок",
                                "п'ятдесят", "шістдесят", "сімдесят", "вісімдесят", "дев'яносто"};
  static const char * teens[] = {"десять",       "одинадцять",   "дванадцять",  "тринадцять",
                                 "чотирнадцять", "п'ятнадцять",  "шістнадцять", "сімнадцять",
                                 "вісімнадцять", "дев'ятнадцять"};
  QStringList parts;
  const int h = value / 100;
  const int rest = value % 100;
  if (h > 0) {
    parts.push_back(QString::fromUtf8(hundreds[h]));
  }
  if (rest >= 10 && rest <= 19) {
    parts.push_back(QString::fromUtf8(teens[rest - 10]));
  } else {
    const int t = rest / 10;
    const int u = rest % 10;
    if (t >= 2) {
      parts.push_back(QString::fromUtf8(tens[t]));
    }
    if (u > 0) {
      parts.push_back(QString::fromUtf8(unitWord(u, gender)));
    }
  }
  return parts.join(QLatin1Char(' '));
}

QString formByNumber(int n, const char * one, const char * few, const char * many)
{
  const int mod100 = n % 100;
  const int mod10 = n % 10;
  if (mod100 >= 11 && mod100 <= 14) {
    return QString::fromUtf8(many);
  }
  if (mod10 == 1) {
    return QString::fromUtf8(one);
  }
  if (mod10 >= 2 && mod10 <= 4) {
    return QString::fromUtf8(few);
  }
  return QString::fromUtf8(many);
}

void appendGroup(QStringList & parts, int value, Gender gender, const char * one, const char * few,
                 const char * many)
{
  if (value <= 0) {
    return;
  }
  QString words = triadToWords(value, gender);
  if (one != nullptr) {
    words += QLatin1Char(' ');
    words += formByNumber(value, one, few, many);
  }
  parts.push_back(words);
}

QString capitalize(QString text)
{
  if (text.isEmpty()) {
    return text;
  }
  text[0] = text[0].toUpper();
  return text;
}

} // namespace

Result<QString> moneyToUkrainianWords(Money amount)
{
  if (amount.isNegative()) {
    return Result<QString>::fail(
        Error::invalidArgument(QStringLiteral("сума прописом не може бути від'ємною")));
  }
  const std::int64_t cents = amount.cents();
  std::int64_t hryvnia = cents / 100;
  const int kopecks = static_cast<int>(cents % 100);

  QStringList groups;
  const int billions = static_cast<int>(hryvnia / 1000000000LL);
  hryvnia %= 1000000000LL;
  const int millions = static_cast<int>(hryvnia / 1000000LL);
  hryvnia %= 1000000LL;
  const int thousands = static_cast<int>(hryvnia / 1000LL);
  const int rest = static_cast<int>(hryvnia % 1000LL);

  appendGroup(groups, billions, Gender::Masculine, "мільярд", "мільярди", "мільярдів");
  appendGroup(groups, millions, Gender::Masculine, "мільйон", "мільйони", "мільйонів");
  appendGroup(groups, thousands, Gender::Feminine, "тисяча", "тисячі", "тисяч");
  if (rest > 0) {
    groups.push_back(triadToWords(rest, Gender::Feminine));
  }
  if (groups.isEmpty()) {
    groups.push_back(QString::fromUtf8("нуль"));
  }

  const int formNumber = rest;
  QString hryvniaNoun = formByNumber(formNumber, "гривня", "гривні", "гривень");
  QString kopeckNoun = formByNumber(kopecks, "копійка", "копійки", "копійок");

  QString text = groups.join(QLatin1Char(' '));
  text += QLatin1Char(' ');
  text += hryvniaNoun;
  text += QLatin1Char(' ');
  text += QStringLiteral("%1").arg(kopecks, 2, 10, QLatin1Char('0'));
  text += QLatin1Char(' ');
  text += kopeckNoun;
  return Result<QString>::ok(capitalize(text));
}

} // namespace fin
