#pragma once
#include "common.h"

// Location teleporter: a list of places grouped by star system (data\locations.txt plus whatever the
// in-game scan finds), and named saved spots (data\bookmarks.txt).

enum TravelKind   { Place_Planet, Place_Moon, Place_Other, Place_Minor };   // Minor: interiors and small zones, hidden by default
struct TravelPlace    { char system[32]; char name[64]; char entity[96]; double radius; int kind; char body[8]; };
struct TravelBookmark { char system[32]; char name[64]; };

// Where on a planet or moon (a place with a known radius) you arrive. Latitude and longitude are in
// degrees in the body's own frame (z through the north pole, longitude 0 along its x axis), so they
// turn with the body and name the same ground every time.
enum TravelLandingMode {
    Landing_Sunlit,   // under the star's meridian at `latitude`: always daytime
    Landing_North,    // above the north pole (how travel worked before)
    Landing_South,
    Landing_LatLon,   // `latitude`, `longitude`
};
struct TravelLanding { int mode = Landing_Sunlit; float latitude = 30.0f; float longitude = 0.0f; };

// Where you are on the body under you, for the Travel tab (any thread). False off any known body.
struct TravelPosition { char body[96]; double latitude, longitude, altitude; };

void ProcessTravel(DWORD now);   // game thread

// Menu side (any thread)
int  Travel_GetPlaces(TravelPlace* out, int max);
int  Travel_PlacesVersion();                       // changes whenever the place list does
int  Travel_GetBookmarks(TravelBookmark* out, int max);
void Travel_CurrentSystem(char* out, size_t n);
bool Travel_CurrentPosition(TravelPosition& out);
// Game thread: one ray from where you stand to the centre of the body under you (a known radius).
// True with the ground's height over the radius; body and yourHeight are filled either way.
bool Travel_GroundUnder(double& overRadius, double& yourHeight, char* body, size_t n);
bool Travel_Scanning(float& progress);
// `altitude` is metres over the ground where the ground can be found (a ray from above once the body
// has streamed in), else over the body's radius as before.
void Travel_RequestPlace(const TravelPlace& place, float altitude, const TravelLanding& landing = TravelLanding());
void Travel_RequestBookmark(int index);
void Travel_RequestSaveBookmark(const char* name);
void Travel_RequestDeleteBookmark(int index);
void Travel_RequestScan();
