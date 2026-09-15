#include "DXFImportPlugin.h"

#include "ImportDXFDialog.h"
#include "Georeferencing.h"

#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProcess>

#include <QtExt_Directories.h>
#include <QtExt_LanguageHandler.h>

#include <IBK_MessageHandlerRegistry.h>
#include <IBK_StringUtils.h>


const std::string VERSION = "1.1";

namespace {

/*! Reads the world coordinate origin from the project context that SIM-VICUS hands in through
	'projectText'. Returns false if there is no context or it holds no origin, the drawing then
	defines the origin itself.
*/
bool readProjectWorldOrigin(const QString & projectText, IBKMK::Vector3D & origin, int & utmZone, bool & north) {
	if (projectText.trimmed().isEmpty())
		return false;

	TiXmlDocument doc;
	doc.Parse(projectText.toUtf8().constData(), nullptr, TIXML_ENCODING_UTF8);
	if (doc.Error())
		return false;

	const TiXmlElement * root = doc.FirstChildElement("VicusProject");
	if (root == nullptr)
		return false;
	const TiXmlElement * project = root->FirstChildElement("Project");
	if (project == nullptr)
		return false;
	const TiXmlElement * wco = project->FirstChildElement("WorldCoordinateOrigin");
	if (wco == nullptr)
		return false;

	const TiXmlElement * originElement = wco->FirstChildElement("Origin");
	if (originElement == nullptr || originElement->GetText() == nullptr)
		return false;

	// the context comes from SIM-VICUS, but a malformed document must never abort the import
	try {
		origin = IBKMK::Vector3D::fromString(originElement->GetText());

		const char * zone = wco->Attribute("utmZone");
		if (zone != nullptr)
			utmZone = IBK::string2val<int>(std::string(zone));
	}
	catch (...) {
		return false;
	}

	// a project without a world coordinate origin sits at (0,0)
	if (origin.m_x == 0 && origin.m_y == 0)
		return false;

	const char * northAttrib = wco->Attribute("north");
	north = (northAttrib == nullptr) || (std::string(northAttrib) != "0" && std::string(northAttrib) != "false");

	return (utmZone >= 1 && utmZone <= 60);
}

} // namespace


DXFImportPlugin::DXFImportPlugin(QObject *parent) :
	QObject(parent)
{}

bool DXFImportPlugin::import(QWidget * parent, QString& projectText) {

	// ask for filename and check

	QString filename = QFileDialog::getOpenFileName(
				parent,
				tr("Select DXF file"),
				m_dxfFileName,
				tr("DXF files (*.dxf);;All files (*.*)"), nullptr );

	if (filename.isEmpty())
		return false;

//	// convert to dxf ?
//	if (filename.endsWith(".dwg")) {

//		int response = QMessageBox::question(parent, tr("File conversion"), tr("Do you want to convert the dwg-file to dxf-format with SIM-VICUS?"), tr("Convert with SIM-VICUS"), tr("Cancel, I will convert it myself"));
//		if (response != QMessageBox::AcceptRole)
//			return false;

//		unsigned int exitCode = 0;
//		bool success = false;
////		QProgressDialog dlg(tr("Running test-init on NANDRAD project"), tr("Cancel"), 0, 0, parent);
////		dlg.setMinimumDuration(0);
////		dlg.setParent(parent);
////		dlg.show();
////		qApp->processEvents();

//		// create cmd line
//		QFileInfo fileInfo(filename);
//		QDir dir = fileInfo.dir();
//		QString dxfFileName = dir.absoluteFilePath(fileInfo.baseName() + ".dxf");

//		QStringList commandLineArgs;
//		commandLineArgs << filename << dxfFileName;

//		QProcess p;
//		p.start("plugins/DXFImport/dwg2dxf.exe", commandLineArgs);
//		p.waitForFinished(10000);
//		exitCode = (unsigned int)p.exitCode();
//		success = p.exitStatus() == 0 && exitCode == 0;

//		if (!success) {
//			QMessageBox::critical(parent, tr("Conversion Error"), tr("Could not convert dwg file to dxf format! You may try to export a dxf file directly from your CAD software!"));
//			return false;
//		}

//		filename = dxfFileName;
//	}

	QFile f1(filename);
	if (!f1.exists()) {
		QMessageBox::critical(
					parent,
					tr("File not found"),
					tr("The file '%1' does not exist or cannot be accessed.").arg(filename)
					);
		m_dxfFileName.clear();
		return false;
	}

	m_dxfFileName = filename;

	// open dialog
	ImportDXFDialog diag(parent);

	// SIM-VICUS hands in the world coordinate origin of the open project, if it has one. All other
	// objects refer to it, so it must not be moved - the drawing is placed relative to it.
	IBKMK::Vector3D	projectOrigin;
	int				projectUtmZone = 32;
	bool			projectNorth = true;
	if (readProjectWorldOrigin(projectText, projectOrigin, projectUtmZone, projectNorth))
		diag.setProjectWorldOrigin(projectOrigin, projectUtmZone, projectNorth);

	ImportDXFDialog::ImportResults res = diag.importFile(filename);

	if (res == ImportDXFDialog::AddDrawings) {

		TiXmlDocument doc;
		TiXmlDeclaration * decl = new TiXmlDeclaration( "1.1", "UTF-8", "" );
		doc.LinkEndChild( decl );

		TiXmlElement * root = new TiXmlElement( "VicusProject" );
		doc.LinkEndChild(root);

		root->SetAttribute("fileVersion", VERSION);

		TiXmlElement * e = new TiXmlElement("Project");
		root->LinkEndChild(e);

		// a georeferenced drawing defines the world coordinate origin, if the project had none, yet
		if (diag.proposesWorldOrigin()) {
			TiXmlElement * wco = new TiXmlElement("WorldCoordinateOrigin");
			e->LinkEndChild(wco);
			wco->SetAttribute("utmZone", IBK::val2string<int>(diag.worldUtmZone()));
			if (!diag.worldNorth())
				wco->SetAttribute("north", IBK::val2string<bool>(false));
			TiXmlElement::appendSingleAttributeElement(wco, "Origin", nullptr, std::string(),
													   diag.worldOrigin().toString(10));
		}

		TiXmlElement * drs = new TiXmlElement("Drawings");
		e->LinkEndChild(drs);

		/* if file was read successfully, add drawing to project */
		Drawing dr = diag.drawing();
		dr.writeXML(drs);

		// Declare a printer
		TiXmlPrinter printer;

		// attach it to the document you want to convert in to a std::string
		doc.Accept(&printer);

		// Create a std::string and copy your document data in to the string
		std::string str = printer.CStr();

		projectText = QString::fromStdString(str);

//		std::ofstream outFile("C:/Test/imported_drawing.xml");
//		outFile << str;
//		outFile.close();

		return true;
	}

	return false;
}


QString DXFImportPlugin::title() const {
	return tr("Import DXF file");
}

QString DXFImportPlugin::importMenuCaption() const {
	return tr("DXF file ...");
}

void DXFImportPlugin::setLanguage(QString langId, QString appname) {
	QtExt::Directories::appname = appname;
	QtExt::Directories::devdir = appname;

	// initialize resources in dependent libraries
	Q_INIT_RESOURCE(QtExt);

	// *** Create log file directory and setup message handler ***
	QDir baseDir;
	baseDir.mkpath(QtExt::Directories::userDataDir());

	IBK::MessageHandlerRegistry::instance().setMessageHandler( &m_messageHandler );
	std::string errmsg;
	std::string logfile = QtExt::Directories::userDataDir().toStdString();
	logfile += "/DXFImportPlugin.log";
	m_messageHandler.openLogFile(logfile, false, errmsg);

	// reset the appname here, so that the correct translation file can be found
	QtExt::Directories::appname = "DXFImportPlugin";
	QtExt::LanguageHandler::instance().installTranslator(langId);
}

QString DXFImportPlugin::DXFFileName() const {
	return m_dxfFileName;
}

