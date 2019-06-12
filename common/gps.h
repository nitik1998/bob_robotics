//GPS


#include "nmea_parser.h"
#include "serial_reader.h"
#include "../third_party/units.h"
#include "map_coordinate.h"




class Gps {

    using degree_t = units::angle::degree_t;
    using meter_t = units::length::meter_t;
    using meters_per_second_t = units::velocity::meters_per_second_t;
    private:
    
    const char *m_device_path;
    int m_currentNumberOfSats;
    int m_gpsQuality;
    double m_horizontalDilution;
    meter_t m_altitude;
    degree_t m_longitude;
    degree_t m_latitude;
    meters_per_second_t m_velocity;

    bool getGPSPosition() {
        using namespace units::angle;
        using namespace units::literals;

        arcminute_t latmin;
        arcminute_t longmin;
        degree_t lat;
        degree_t longitude;
        char latDir;
        char longDir;

        // reading and parsing serial data
        std::string serialString = Serial_reader::readSerialUSB(m_device_path);
        bool didParse = NMEAParser::parseTextGPS(serialString, lat, latmin, latDir, longitude, longmin, longDir, m_altitude, m_velocity);

        m_latitude = lat + latmin;
        m_longitude = longitude + longmin;

        // West and South has negative angles
        if (longDir == 'W') m_longitude = - m_longitude;
        if (latDir == 'S')  m_latitude  = - m_latitude;

        return didParse;

    }

    bool getGpsDescription()  {
        std::string serialString =  Serial_reader::readSerialUSB(m_device_path);
        int quality, numsat;
        double horizontalDilution;
        bool didParse = NMEAParser::parseTextForMiscData(serialString, quality, numsat, horizontalDilution);
        m_gpsQuality = quality;
        m_currentNumberOfSats = numsat;
        m_horizontalDilution = horizontalDilution;

        return didParse;
    }


    public:

    enum GPSQuality
    {
        INVALID,  
        GPSFIX,  
        DGPSFIX,
        PPSFIX,
        RTK,
        FRTK,
    };

    Gps(const char *device_path) : m_device_path(device_path)  { }

    bool getPosition(degree_t &lat, degree_t &lon, meter_t &altitude) {
        bool didGetGps = getGPSPosition(); 
        lat = m_latitude;
        lon = m_longitude;
        altitude = m_altitude;

        return didGetGps;
        
    }

    int getNumberOfSatelites() {
        getGpsDescription();
        return m_currentNumberOfSats;
    }

    GPSQuality getGpsQuality() {
        getGpsDescription();
        GPSQuality quality;    
        switch(m_gpsQuality) {
            case 0 : quality = INVALID; break;
            case 1 : quality = GPSFIX; break;
            case 2 : quality = DGPSFIX; break;
            case 3 : quality = PPSFIX; break;
            case 4 : quality = RTK; break;
            case 5 : quality = FRTK; break;
        }
        return quality;
    }

    double getHorizontalDilution() {
        getGpsDescription();
        return m_horizontalDilution;
    }


   

};