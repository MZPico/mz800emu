#ifndef UI_CMT_HPP
#define UI_CMT_HPP

#include <vector>
#include <string>

#include "libs/cmtspeed/cmtspeed.h"
#include "cmt/cmtext_block.h"

typedef enum en_PLAY_STATE
{
    PLAY_STATE_NONE = 0,
    PLAY_STATE_STOPPED,
    PLAY_STATE_PLAYING,
    PLAY_STATE_PAUSED,
    PLAY_STATE_COUNT_ITEMS
} en_PLAY_STATE;

typedef enum en_CMT_SPEED_TYPE
{
    CMT_SPEED_TYPE_NONE = 0,
    CMT_SPEED_TYPE_MZ,
    CMT_SPEED_TYPE_ZX,
} en_CMT_SPEED_TYPE;

typedef struct TapeFileEntry_t
{
    int id;
    en_PLAY_STATE play_state;
    bool ready;
    bool played;
    bool paused;
    std::string name;
    std::string ftype_txt;
    std::string fsize_txt;
    std::string fstrt_txt;
    std::string fexec_txt;
    int block_type;
    en_CMTEXT_BLOCK_SPEED block_speed; // CMTEXT_BLOCK_SPEED_NONE, CMTEXT_BLOCK_SPEED_DEFAULT, CMTEXT_BLOCK_SPEED_SET
    en_CMTSPEED cmtspeed;              // CMTSPEED_NONE, CMTSPEED_1_1, CMTSPEED_2_1, CMTSPEED_2_1_CPM, CMTSPEED_3_1, CMTSPEED_3_2, CMTSPEED_7_3, CMTSPEED_8_3, CMTSPEED_9_7, CMTSPEED_25_14
    std::string cmtspeed_txt;          // "1:1 - 1200 Bd", "2:1 - 2400 Bd", "2:1 - 2400 Bd (cp/m)", "7:3 - 2800 Bd", "8:3 - 3200 Bd", "3:1 - 3600 Bd"
    en_CMT_SPEED_TYPE cmtspeed_type;   // CMT_SPEED_TYPE_NONE, CMT_SPEED_TYPE_MZ, CMT_SPEED_TYPE_ZX
    std::string length_txt;
} TapeFileEntry_t;

extern std::vector<TapeFileEntry_t> g_tapeFileList;

typedef struct UiCmtSpeedList_t
{
    en_CMTSPEED cmtspeed;
    std::string cmtspeed_txt;
} UiCmtSpeedList_t;

class UiCmt
{
public:
    /// Zakázání vytváření instancí.
    UiCmt() = delete;

    /// Zakázání kopírování a přiřazování.
    UiCmt(const UiCmt &) = delete;
    UiCmt &operator=(const UiCmt &) = delete;

    static std::string getSpeedTxt(en_CMTEXT_BLOCK_TYPE bltype, en_CMTEXT_BLOCK_SPEED blspeed, en_CMTSPEED cmtspeed, bool get_rateiospeed);

    // ziskani pole s rychlostmi pro MZ
    static std::vector<UiCmtSpeedList_t> getMzSpeedList(void);
    static std::vector<UiCmtSpeedList_t> getMzSpeedListDefault(void);

    // ziskani pole s rychlostmi pro ZX
    static std::vector<UiCmtSpeedList_t> getZxSpeedList(void);

    // ziskani delky bloku v sekundach
    static int getLengthInSec(en_CMTEXT_BLOCK_TYPE bltype, en_CMTEXT_BLOCK_SPEED blspeed, en_CMTSPEED cmtspeed, int fsize);

    // aktualizace seznamu souboru na pasce
    static void updateTapeFilelist(void);
};

#endif // UI_CMT_HPP
