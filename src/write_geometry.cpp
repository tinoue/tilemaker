/*
	WriteGeometryVisitor
	- takes a boost::geometry object and writes it into a tile
*/

typedef vector<pair<int,int>> XYString;

class WriteGeometryVisitor : public boost::static_visitor<> { public:

	TileBbox *bboxPtr;
	vector_tile::Tile_Feature *featurePtr;
	double simplifyLevel;

	WriteGeometryVisitor(TileBbox *bp, vector_tile::Tile_Feature *fp, double sl) {
		bboxPtr = bp;
		featurePtr = fp;
		simplifyLevel = sl;
	}

	// Point
	void operator()(Point &p) const {
		if (geom::within(p, bboxPtr->clippingBox)) {
			featurePtr->add_geometry(9);					// moveTo, repeat x1
			pair<int,int> xy = bboxPtr->scaleLatpLon(p.y(), p.x());
			featurePtr->add_geometry((xy.first  << 1) ^ (xy.first  >> 31));
			featurePtr->add_geometry((xy.second << 1) ^ (xy.second >> 31));
			featurePtr->set_type(vector_tile::Tile_GeomType_POINT);
		}
	}

	// Multipolygon
	void operator()(MultiPolygon &mp) const {
		if (simplifyLevel>0) {
			MultiPolygon simplified;
			geom::simplify(mp, simplified, simplifyLevel);
			mp = simplified;
		}

		pair<int,int> lastPos(0,0);
		for (MultiPolygon::const_iterator it = mp.begin(); it != mp.end(); ++it) {
			XYString scaledString;
			Ring ring = geom::exterior_ring(*it);
			for (auto jt = ring.begin(); jt != ring.end(); ++jt) {
				pair<int,int> xy = bboxPtr->scaleLatpLon(jt->get<1>(), jt->get<0>());
				scaledString.push_back(xy);
			}
			writeDeltaString(&scaledString, featurePtr, &lastPos, true);

			InteriorRing interiors = geom::interior_rings(*it);
			for (auto ii = interiors.begin(); ii != interiors.end(); ++ii) {
				scaledString.clear();
				XYString scaledInterior;
				for (auto jt = ii->begin(); jt != ii->end(); ++jt) {
					pair<int,int> xy = bboxPtr->scaleLatpLon(jt->get<1>(), jt->get<0>());
					scaledString.push_back(xy);
				}
				writeDeltaString(&scaledString, featurePtr, &lastPos, true);
			}
		}
		featurePtr->set_type(vector_tile::Tile_GeomType_POLYGON);
	}

	// Multilinestring
	void operator()(MultiLinestring &mls) const {
		if (simplifyLevel>0) {
			MultiLinestring simplified;
			geom::simplify(mls, simplified, simplifyLevel);
			mls = simplified;
		}

		pair<int,int> lastPos(0,0);
		for (MultiLinestring::const_iterator it = mls.begin(); it != mls.end(); ++it) {
			XYString scaledString;
			for (Linestring::const_iterator jt = it->begin(); jt != it->end(); ++jt) {
				pair<int,int> xy = bboxPtr->scaleLatpLon(jt->get<1>(), jt->get<0>());
				scaledString.push_back(xy);
			}
			writeDeltaString(&scaledString, featurePtr, &lastPos, false);
		}
		featurePtr->set_type(vector_tile::Tile_GeomType_LINESTRING);
	}

	// Linestring
	void operator()(Linestring &ls) const { 
		if (simplifyLevel>0) {
			Linestring simplified;
			geom::simplify(ls, simplified, simplifyLevel);
			ls = simplified;
		}

		pair<int,int> lastPos(0,0);
		XYString scaledString;
		for (Linestring::const_iterator jt = ls.begin(); jt != ls.end(); ++jt) {
			pair<int,int> xy = bboxPtr->scaleLatpLon(jt->get<1>(), jt->get<0>());
			scaledString.push_back(xy);
		}
		writeDeltaString(&scaledString, featurePtr, &lastPos, false);
		featurePtr->set_type(vector_tile::Tile_GeomType_LINESTRING);
	}

	// Encode a series of pixel co-ordinates into the feature, using delta and zigzag encoding
	void writeDeltaString(XYString *scaledString, vector_tile::Tile_Feature *featurePtr, pair<int,int> *lastPos, bool closePath) const {
		if (scaledString->size()<2) return;
		vector<uint32_t> geometry;

		// Start with a moveTo
		int lastX = scaledString->at(0).first;
		int lastY = scaledString->at(0).second;
		int dx = lastX - lastPos->first;
		int dy = lastY - lastPos->second;
		geometry.push_back(9);						// moveTo, repeat x1
		geometry.push_back((dx << 1) ^ (dx >> 31));
		geometry.push_back((dy << 1) ^ (dy >> 31));

		// Then write out the line for each point
		uint len=0;
		geometry.push_back(0);						// this'll be our lineTo opcode, we set it later
		for (uint i=1; i<scaledString->size(); i++) {
			int x = scaledString->at(i).first;
			int y = scaledString->at(i).second;
			if (x==lastX && y==lastY) { continue; }
			dx = x-lastX;
			dy = y-lastY;
			geometry.push_back((dx << 1) ^ (dx >> 31));
			geometry.push_back((dy << 1) ^ (dy >> 31));
			lastX = x; lastY = y;
			len++;
		}
		if (len==0) return;
		geometry[3] = (len << 3) + 2;				// lineTo plus repeat
		if (closePath) {
			geometry.push_back(7+8);				// closePath
		}
		for (uint i=0; i<geometry.size(); i++) { 
			featurePtr->add_geometry(geometry[i]);
		};
		lastPos->first  = lastX;
		lastPos->second = lastY;
	}
};
