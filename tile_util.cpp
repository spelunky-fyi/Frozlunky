#include <stdexcept>
#include <Windows.h>
#include "tile_util.h"

namespace TileUtil {
	std::string QueryTileFile(bool save) {
		char szFile[MAX_PATH];
		szFile[0] = '\0';

		OPENFILENAME ofn;
		ZeroMemory(&ofn, sizeof(OPENFILENAME));
	
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = "XML Chunk Format (*.xml *.txt)\0*.xml;*.txt\0";
		
		ofn.Flags  = OFN_PATHMUSTEXIST;
		if(!save) {
			ofn.Flags |= OFN_FILEMUSTEXIST;
		}

		BOOL res;
		if(save)
			res = GetSaveFileName(&ofn);
		else
			res = GetOpenFileName(&ofn);

		if(!res) {
			throw std::runtime_error("File open failed.");
		}

		std::string ret(szFile);
		if(ret.find(".xml") != ret.size() - 4 && ret.find(".txt") != ret.size() - 4) {
			ret += ".xml";
		}

		return ret;
	}

	std::string GetBaseFilename(const std::string& file) {
		auto las = file.find_last_of("\\/");
		if(las != std::string::npos) {
			return file.substr(las+1);
		}
		else {
			return file;
		}
	}
}