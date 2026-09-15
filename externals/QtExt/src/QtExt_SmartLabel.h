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

#ifndef QtExt_SmartLabelH
#define QtExt_SmartLabelH

#include <QLabel>
#include <QSize>

#include "QtExt_global.h"

namespace QtExt {

/*! A label styled as a notice box with an icon, palette-derived background
	color and a rounded border.  Supports four modes: Info, Warning, Critical
	and Success, each with its own icon and color scheme.

	The widget paints itself entirely in paintEvent: rounded background,
	SVG icon (re-tinted in dark mode) and word-wrapped text laid out via
	QTextLayout. The QLabel base is kept for accessibility and Designer
	promotion compatibility; QLabel's own text rendering is bypassed.
*/
class QtExt_EXPORT SmartLabel : public QLabel {
	Q_OBJECT
	Q_PROPERTY(Mode mode READ mode WRITE setMode)
public:
	/*! Display mode that controls icon and color scheme. */
	enum Mode {
		/*! Blue info circle with 'i'. */
		SM_Info,
		/*! Orange/yellow warning triangle with '!'. */
		SM_Warning,
		/*! Red circle with '!'. */
		SM_Critical,
		/*! Green circle with checkmark. */
		SM_Success,
		NUM_SM
	};
	Q_ENUM(Mode)

	explicit SmartLabel(QWidget * parent = nullptr);
	explicit SmartLabel(const QString & text, QWidget * parent = nullptr);

	/*! Switches color scheme between light and dark mode.
		Call this on startup and whenever the theme changes.
	*/
	static void setDarkMode(bool dark);

	/*! Controls global visibility of all SmartLabels.
		When set to false, all SmartLabels collapse to zero size and skip painting.
	*/
	static void setSmartLabelsVisible(bool visible);
	/*! Returns global visibility state. */
	static bool smartLabelsVisible();

	/*! Returns the current display mode. */
	Mode mode() const { return m_mode; }
	/*! Sets the display mode and re-applies styling. */
	void setMode(Mode mode);

	/*! Sets the displayed text. */
	void setText(const QString & text);
	/*! Clears the label text. */
	void clear();

	/*! Width-dependent height of the wrapped text plus icon and padding. */
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override;

	QSize minimumSizeHint() const override;
	QSize sizeHint() const override;

protected:
	/*! Deferred initialization on first show. */
	void showEvent(QShowEvent * event) override;
	/*! Custom rendering: rounded background, SVG icon and wrapped text. */
	void paintEvent(QPaintEvent * event) override;

private:
	/*! Returns true if the label should currently be hidden (collapsed) by the
		global visibility flag. */
	bool isCollapsed() const;

	/*! Computes the wrapped-text height for the given total widget width. */
	int textHeightForWidth(int totalWidth) const;

	/*! Stores the plain text used for layout and painting. */
	QString						m_plainText;

	/*! True once the widget has been shown and fully initialized. */
	bool						m_initialized = false;

	/*! Current display mode. */
	Mode						m_mode = SM_Info;

};

} // namespace QtExt

#endif // QtExt_SmartLabelH
