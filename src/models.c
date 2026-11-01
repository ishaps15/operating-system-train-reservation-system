#include "models.h"
#include <string.h>
#include <strings.h>

const char* role_to_str(UserRole role) {
    switch (role) {
        case ROLE_PASSENGER: return "PASSENGER";
        case ROLE_AGENT:     return "BOOKING_AGENT";
        case ROLE_ADMIN:     return "ADMINISTRATOR";
        case ROLE_TTE:       return "TTE_CONDUCTOR";
        default:             return "UNKNOWN";
    }
}

const char* class_to_str(TravelClass tc) {
    switch (tc) {
        case CLASS_1A: return "AC First Class (1A)";
        case CLASS_2A: return "AC 2-Tier (2A)";
        case CLASS_3A: return "AC 3-Tier (3A)";
        case CLASS_CC: return "AC Chair Car (CC)";
        case CLASS_SL: return "Sleeper Class (SL)";
        case CLASS_2S: return "Second Sitting (2S)";
        default:       return "Unknown Class";
    }
}

const char* class_code_to_str(TravelClass tc) {
    switch (tc) {
        case CLASS_1A: return "1A";
        case CLASS_2A: return "2A";
        case CLASS_3A: return "3A";
        case CLASS_CC: return "CC";
        case CLASS_SL: return "SL";
        case CLASS_2S: return "2S";
        default:       return "??";
    }
}

TravelClass parse_class_code(const char *code) {
    if (!code) return CLASS_SL;
    if (strcasecmp(code, "1A") == 0) return CLASS_1A;
    if (strcasecmp(code, "2A") == 0) return CLASS_2A;
    if (strcasecmp(code, "3A") == 0) return CLASS_3A;
    if (strcasecmp(code, "CC") == 0) return CLASS_CC;
    if (strcasecmp(code, "SL") == 0) return CLASS_SL;
    if (strcasecmp(code, "2S") == 0) return CLASS_2S;
    return CLASS_SL;
}

const char* berth_to_str(BerthType bt) {
    switch (bt) {
        case BERTH_LOWER:      return "Lower";
        case BERTH_MIDDLE:     return "Middle";
        case BERTH_UPPER:      return "Upper";
        case BERTH_SIDE_LOWER: return "Side Lower";
        case BERTH_SIDE_UPPER: return "Side Upper";
        case BERTH_WINDOW:     return "Window Seat";
        case BERTH_AISLE:      return "Aisle Seat";
        case BERTH_CABIN:      return "Cabin Berth";
        default:               return "Unassigned";
    }
}

const char* status_to_str(BookingStatus status) {
    switch (status) {
        case STATUS_CNF:       return "CNF (Confirmed)";
        case STATUS_RAC:       return "RAC";
        case STATUS_WL:        return "WL (Waiting List)";
        case STATUS_CANCELLED: return "CANCELLED";
        default:               return "UNKNOWN";
    }
}

const char* quota_to_str(QuotaType q) {
    switch (q) {
        case QUOTA_GENERAL: return "General (GN)";
        case QUOTA_TATKAL:  return "Tatkal (TQ)";
        case QUOTA_LADIES:  return "Ladies (LD)";
        case QUOTA_SENIOR:  return "Senior Citizen (SS)";
        case QUOTA_DEFENSE: return "Defense (DF)";
        default:            return "General";
    }
}

const char* concession_to_str(ConcessionType c) {
    switch (c) {
        case CONC_NONE:           return "None";
        case CONC_SENIOR_CITIZEN: return "Senior Citizen Concession";
        case CONC_CHILD:          return "Child Half-Fare";
        case CONC_STUDENT:        return "Student Pass Concession";
        case CONC_DEFENSE:        return "Armed Forces Concession";
        default:                  return "None";
    }
}
