#include "String.h"
#include <QTextStream>
#include <iostream>

String String::xmlEncode() const {
	return String(*this)
		.replace('&', "&amp;")
		.replace("<", "&lt;")
		.replace(">", "&gt;")
		.replace("\"", "&quot;");
}

std::ostream &operator <<(std::ostream &os, String const &s) {
	return os << s.constData();
}
