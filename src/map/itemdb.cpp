#include "itemdb.hpp"

#include <cstdlib>
#include <cstring>

#include "../compat/nullpo.hpp"

#include "../strings/astring.hpp"
#include "../strings/zstring.hpp"
#include "../strings/xstring.hpp"

#include "../generic/db.hpp"
#include "../generic/random.hpp"

#include "../io/cxxstdio.hpp"
#include "../io/read.hpp"

#include "../mmo/config_parse.hpp"
#include "../mmo/extract.hpp"
#include "../mmo/socket.hpp"

#include "../poison.hpp"

constexpr int MAX_RANDITEM = 2000;

// ** ITEMDB_OVERRIDE_NAME_VERBOSE **
//   定義すると、itemdb.txtとgrfで名前が異なる場合、表示します.
//#define ITEMDB_OVERRIDE_NAME_VERBOSE  1

static
Map<int, struct item_data> item_db;

// Function declarations

/*==========================================
 * 名前で検索用
 *------------------------------------------
 */
// name = item alias, so we should find items aliases first. if not found then look for "jname" (full name)
static
void itemdb_searchname_sub(struct item_data *item, ItemName str, struct item_data **dst)
{
    if (item->name == str)
        *dst = item;
}

/*==========================================
 * 名前で検索
 *------------------------------------------
 */
struct item_data *itemdb_searchname(XString str_)
{
    ItemName str = stringish<ItemName>(str_);
    if (XString(str) != str_)
        return nullptr;
    struct item_data *item = NULL;
    for (auto& pair : item_db)
        itemdb_searchname_sub(&pair.second, str, &item);
    return item;
}

/*==========================================
 * DBの存在確認
 *------------------------------------------
 */
struct item_data *itemdb_exists(int nameid)
{
    return item_db.search(nameid);
}

/*==========================================
 * DBの検索
 *------------------------------------------
 */
struct item_data *itemdb_search(int nameid)
{
    struct item_data *id = item_db.search(nameid);
    if (id)
        return id;

    id = item_db.init(nameid);

    id->nameid = nameid;
    id->value_buy = 10;
    id->value_sell = id->value_buy / 2;
    id->weight = 10;
    id->sex = SEX::NEUTRAL;
    id->elv = 0;

    if (nameid > 500 && nameid < 600)
        id->type = ItemType::USE;
    else if (nameid > 600 && nameid < 700)
        id->type = ItemType::_2;
    else if ((nameid > 700 && nameid < 1100) ||
             (nameid > 7000 && nameid < 8000))
        id->type = ItemType::JUNK;
    else if (nameid >= 1750 && nameid < 1771)
        id->type = ItemType::ARROW;
    else if (nameid > 1100 && nameid < 2000)
        id->type = ItemType::WEAPON;
    else if ((nameid > 2100 && nameid < 3000) ||
             (nameid > 5000 && nameid < 6000))
        id->type = ItemType::ARMOR;
    else if (nameid > 4000 && nameid < 5000)
        id->type = ItemType::_6;

    return id;
}

/*==========================================
 *
 *------------------------------------------
 */
int itemdb_isequip(int nameid)
{
    ItemType type = itemdb_type(nameid);
    return !(type == ItemType::USE
        || type == ItemType::_2
        || type == ItemType::JUNK
        || type == ItemType::_6
        || type == ItemType::ARROW);
}

/*==========================================
 *
 *------------------------------------------
 */
int itemdb_isequip2(struct item_data *data)
{
    if (!data)
        return false;
    ItemType type = data->type;
    return !(type == ItemType::USE
        || type == ItemType::_2
        || type == ItemType::JUNK
        || type == ItemType::_6
        || type == ItemType::ARROW);
}

/*==========================================
 *
 *------------------------------------------
 */
int itemdb_isequip3(int nameid)
{
    ItemType type = itemdb_type(nameid);
    return (type == ItemType::WEAPON
        || type == ItemType::ARMOR
        || type == ItemType::_8);
}

bool itemdb_readdb(ZString filename)
{
    bool rv = true;

    int ln = 0, lines = 0;

    {
        io::ReadFile in(filename);

        if (!in.is_open())
        {
            PRINTF("can't read %s\n", filename);
            return false;
        }

        lines = 0;

        AString line;
        while (in.getline(line))
        {
            lines++;
            if (is_comment(line))
                continue;
            // a line is 17 normal fields followed by 2 {} fields
            // the fields are separated by ", *", but there may be ,
            // in the {}.

            auto it = std::find(line.begin(), line.end(), '{');
            XString main_part = line.xislice_h(it).rstrip();
            // According to the code, tail_part may be empty. See later.
            ZString tail_part = line.xislice_t(it);

            XString unused_slot_count;
            item_data idv {};
            if (!extract(
                        main_part, record<','>(
                            &idv.nameid,
                            lstripping(&idv.name),
                            lstripping(&idv.jname),
                            lstripping(&idv.type),
                            lstripping(&idv.value_buy),
                            lstripping(&idv.value_sell),
                            lstripping(&idv.weight),
                            lstripping(&idv.atk),
                            lstripping(&idv.def),
                            lstripping(&idv.range),
                            lstripping(&idv.magic_bonus),
                            lstripping(&unused_slot_count),
                            lstripping(&idv.sex),
                            lstripping(&idv.equip),
                            lstripping(&idv.wlv),
                            lstripping(&idv.elv),
                            lstripping(&idv.look)
                        )
                    )
            )
            {
                PRINTF("%s:%d: error: bad item line: %s\n",
                        filename, lines, line);
                rv = false;
                continue;
            }

            ln++;

            struct item_data *id = itemdb_search(idv.nameid);
            *id = std::move(idv);
            if (id->value_buy == 0 && id->value_sell == 0)
            {
            }
            else if (id->value_buy == 0)
            {
                id->value_buy = id->value_sell * 2;
            }
            else if (id->value_sell == 0)
            {
                id->value_sell = id->value_buy / 2;
            }

            id->use_script = NULL;
            id->equip_script = NULL;

            if (!tail_part)
                continue;
            id->use_script = parse_script(tail_part, lines);

            tail_part = tail_part.xislice_t(std::find(tail_part.begin() + 1, tail_part.end(), '{'));
            if (!tail_part)
                continue;
            id->equip_script = parse_script(tail_part, lines);
        }
        PRINTF("read %s done (count=%d)\n", filename, ln);
    }

    return rv;
}

/*==========================================
 *
 *------------------------------------------
 */
static
void itemdb_final(struct item_data *id)
{
    id->use_script.reset();
    id->equip_script.reset();
}

/*==========================================
 *
 *------------------------------------------
 */
void do_final_itemdb(void)
{
    for (auto& pair : item_db)
        itemdb_final(&pair.second);
    item_db.clear();
}
