/*	QtExt - Qt-based utility classes and functions (extends Qt library)

	Copyright (c) 2014-today, Institut für Bauklimatik, TU Dresden, Germany

	Primary authors:
	  Heiko Fechner    <heiko.fechner -[at]- tu-dresden.de>
	  Andreas Nicolai

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

	Dieses Programm ist Freie Software: Sie können es unter den Bedingungen
	der GNU General Public License, wie von der Free Software Foundation,
	Version 3 der Lizenz oder (nach Ihrer Wahl) jeder neueren
	veröffentlichten Version, weiter verteilen und/oder modifizieren.

	Dieses Programm wird in der Hoffnung bereitgestellt, dass es nützlich sein wird, jedoch
	OHNE JEDE GEWÄHR,; sogar ohne die implizite
	Gewähr der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
	Siehe die GNU General Public License für weitere Einzelheiten.

	Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
	Programm erhalten haben. Wenn nicht, siehe <https://www.gnu.org/licenses/>.
*/

#include "QtExt_SmartLabel.h"

#include <cmath>

#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTextLayout>
#include <QTextOption>
#include <QtSvg/QSvgRenderer>

namespace QtExt {

// Layout constants (in device-independent pixels).
static constexpr int PADDING		= 4;	// outer padding on all sides
static constexpr int ICON_SIZE		= 16;	// icon edge length
static constexpr int ICON_GAP		= 6;	// gap between icon and text
static constexpr int CORNER_RADIUS	= 4;

/*! Per-mode text color, background color and SVG icon path. */
struct ModeStyle {
	const char	*m_textColor;
	const char	*m_bgColor;
	const char	*m_iconPath;
};

static const ModeStyle MODE_STYLES_LIGHT[SmartLabel::NUM_SM] = {
	// SM_Info     - steel blue
	{ "#5580a0",  "#e8eef4",  ":/gfx/smartlabel_info.svg" },
	// SM_Warning  - amber
	{ "#c89620",  "#faf3e0",  ":/gfx/smartlabel_warning.svg" },
	// SM_Critical - red
	{ "#c04040",  "#f8e8e8",  ":/gfx/smartlabel_critical.svg" },
	// SM_Success  - green
	{ "#2da060",  "#e4f5ec",  ":/gfx/smartlabel_success.svg" },
};

static const ModeStyle MODE_STYLES_DARK[SmartLabel::NUM_SM] = {
	// SM_Info     - muted steel blue on dark blue-gray
	{ "#8ab4cc",  "#2a3540",  ":/gfx/smartlabel_info.svg" },
	// SM_Warning  - muted amber on dark brown
	{ "#d4a84a",  "#3a3020",  ":/gfx/smartlabel_warning.svg" },
	// SM_Critical - muted red on dark red
	{ "#d07070",  "#3a2020",  ":/gfx/smartlabel_critical.svg" },
	// SM_Success  - muted green on dark green
	{ "#60c090",  "#203a28",  ":/gfx/smartlabel_success.svg" },
};

static bool s_smartLabelsVisible	= false;
static bool s_darkMode				= false;

static const ModeStyle & styleFor(SmartLabel::Mode mode) {
	return s_darkMode ? MODE_STYLES_DARK[mode] : MODE_STYLES_LIGHT[mode];
}


/*! Renders an SVG icon to a pixmap at the requested device-pixel ratio.
	In dark mode the SVG silhouette is re-tinted with the mode's text color so
	icons stay legible against the dark background. The result is cached per
	(mode, dark-flag, dpr) tuple. */
static QPixmap iconPixmap(SmartLabel::Mode mode, qreal dpr) {
	static QHash<quint64, QPixmap> cache;
	const quint64 dprKey = static_cast<quint64>(qRound(dpr * 100.0));
	const quint64 key = (static_cast<quint64>(mode) << 16)
						| (static_cast<quint64>(s_darkMode ? 1 : 0) << 8)
						| dprKey;
	auto it = cache.constFind(key);
	if (it != cache.constEnd())
		return *it;

	const ModeStyle & ms = styleFor(mode);
	QSvgRenderer renderer(QString::fromLatin1(ms.m_iconPath));
	QImage img(QSize(ICON_SIZE, ICON_SIZE) * dpr, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	{
		QPainter p(&img);
		p.setRenderHint(QPainter::Antialiasing);
		renderer.render(&p, QRectF(0, 0, img.width(), img.height()));
	}
	if (s_darkMode) {
		// Re-tint the silhouette with the foreground color of the dark scheme.
		QPainter p(&img);
		p.setCompositionMode(QPainter::CompositionMode_SourceIn);
		p.fillRect(img.rect(), QColor(ms.m_textColor));
	}
	QPixmap pm = QPixmap::fromImage(img);
	pm.setDevicePixelRatio(dpr);
	cache.insert(key, pm);
	return pm;
}


SmartLabel::SmartLabel(QWidget * parent) :
	QLabel(parent)
{
	setTextFormat(Qt::PlainText);
	setMargin(0);
	setWordWrap(false);				// we do our own wrapping
	setAttribute(Qt::WA_TranslucentBackground, false);
	QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
	sp.setHeightForWidth(true);
	setSizePolicy(sp);
}


SmartLabel::SmartLabel(const QString & text, QWidget * parent) :
	QLabel(parent),
	m_plainText(text)
{
	setTextFormat(Qt::PlainText);
	setMargin(0);
	setWordWrap(false);
	setAttribute(Qt::WA_TranslucentBackground, false);
	QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
	sp.setHeightForWidth(true);
	setSizePolicy(sp);
	QLabel::setText(text);			// keep base in sync for a11y
}


void SmartLabel::setText(const QString & text) {
	if (m_plainText == text)
		return;
	m_plainText = text;
	QLabel::setText(text);			// keep base in sync for a11y / tooltip
	updateGeometry();
	update();
}


void SmartLabel::clear() {
	m_plainText.clear();
	QLabel::clear();
	updateGeometry();
	update();
}


void SmartLabel::setMode(Mode mode) {
	if (m_mode == mode)
		return;
	m_mode = mode;
	update();
}


void SmartLabel::setSmartLabelsVisible(bool visible) {
	s_smartLabelsVisible = visible;
}


bool SmartLabel::smartLabelsVisible() {
	return s_smartLabelsVisible;
}


void SmartLabel::setDarkMode(bool dark) {
	s_darkMode = dark;
}


int SmartLabel::heightForWidth(int w) const {
	if (isCollapsed() || m_plainText.isEmpty())
		return 0;
	const int textH = textHeightForWidth(w);
	const int contentH = std::max(textH, ICON_SIZE);
	return contentH + 2 * PADDING;
}


QSize SmartLabel::minimumSizeHint() const {
	if (isCollapsed())
		return QSize(0, 0);
	// Minimum width: padding + icon + gap + ~10 chars of text.
	const int textMin = QFontMetrics(font()).averageCharWidth() * 10;
	const int w = 2 * PADDING + ICON_SIZE + ICON_GAP + textMin;
	// Minimum height: one line; layout uses heightForWidth() for the actual height.
	const int h = 2 * PADDING + ICON_SIZE;
	return QSize(w, h);
}


QSize SmartLabel::sizeHint() const {
	if (isCollapsed())
		return QSize(0, 0);
	// Preferred width: wide enough that typical info text wraps to 1-2 lines.
	const int textPref = QFontMetrics(font()).averageCharWidth() * 40;
	const int w = 2 * PADDING + ICON_SIZE + ICON_GAP + textPref;
	return QSize(w, heightForWidth(w));
}


void SmartLabel::showEvent(QShowEvent * event) {
	if (!m_initialized) {
		m_initialized = true;
		// Pick up any text that was set via the .ui file before show.
		if (m_plainText.isEmpty() && !QLabel::text().isEmpty())
			m_plainText = QLabel::text();
	}
	if (isCollapsed())
		setMaximumHeight(0);
	else
		setMaximumHeight(QWIDGETSIZE_MAX);
	QLabel::showEvent(event);
}


void SmartLabel::paintEvent(QPaintEvent * /*event*/) {
	if (isCollapsed() || m_plainText.isEmpty())
		return;

	const ModeStyle & ms = styleFor(m_mode);
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	// Background.
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(ms.m_bgColor));
	p.drawRoundedRect(rect(), CORNER_RADIUS, CORNER_RADIUS);

	// Icon (top-aligned with first text line).
	const QPixmap pm = iconPixmap(m_mode, devicePixelRatioF());
	const QRect iconRect(PADDING, PADDING, ICON_SIZE, ICON_SIZE);
	p.drawPixmap(iconRect, pm);

	// Text area to the right of the icon.
	const int textX = PADDING + ICON_SIZE + ICON_GAP;
	const int textWidth = std::max(0, width() - textX - PADDING);
	if (textWidth <= 0)
		return;

	QTextLayout layout(m_plainText, font());
	QTextOption opt;
	opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	opt.setAlignment(Qt::AlignLeft | Qt::AlignTop);
	layout.setTextOption(opt);

	qreal y = 0;
	layout.beginLayout();
	while (true) {
		QTextLine line = layout.createLine();
		if (!line.isValid())
			break;
		line.setLineWidth(textWidth);
		line.setPosition(QPointF(textX, PADDING + y));
		y += line.height();
	}
	layout.endLayout();

	p.setPen(QColor(ms.m_textColor));
	layout.draw(&p, QPointF(0, 0));
}


bool SmartLabel::isCollapsed() const {
	return !s_smartLabelsVisible && m_mode == SM_Info;
}


int SmartLabel::textHeightForWidth(int totalWidth) const {
	const int textWidth = totalWidth - 2 * PADDING - ICON_SIZE - ICON_GAP;
	if (textWidth <= 0 || m_plainText.isEmpty())
		return 0;

	QTextLayout layout(m_plainText, font());
	QTextOption opt;
	opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	opt.setAlignment(Qt::AlignLeft | Qt::AlignTop);
	layout.setTextOption(opt);

	qreal y = 0;
	layout.beginLayout();
	while (true) {
		QTextLine line = layout.createLine();
		if (!line.isValid())
			break;
		line.setLineWidth(textWidth);
		line.setPosition(QPointF(0, y));
		y += line.height();
	}
	layout.endLayout();
	return static_cast<int>(std::ceil(y));
}

} // namespace QtExt
