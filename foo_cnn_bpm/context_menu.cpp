#include "pch.h"

#include "bpm.h"

static void analyse_bpm(const metadb_handle_list_cref &);

class bpm_context_item : public contextmenu_item_simple
{
  public:
    GUID get_parent()
    {
        return contextmenu_groups::utilities;
    }

    unsigned get_num_items()
    {
        return 1;
    }

    void get_item_name(unsigned p_index, pfc::string_base &p_out)
    {
        if (p_index != 0)
        {
            uBugCheck();
        }
        p_out = "Analyse BPM";
    }

    void context_command(unsigned p_index, metadb_handle_list_cref p_data, const GUID &p_caller)
    {
        if (p_index != 0)
        {
            uBugCheck();
        }
        analyse_bpm(p_data);
    }

    GUID get_item_guid(unsigned p_index)
    {
        if (p_index != 0)
        {
            uBugCheck();
        }
        static constexpr GUID guid = {0xc87a901f, 0x315c, 0x4b80, {0xba, 0xba, 0x13, 0x30, 0xf8, 0xd2, 0x0f, 0x6b}};
        return guid;
    }

    bool get_item_description(unsigned p_index, pfc::string_base &p_out)
    {
        if (p_index != 0)
        {
            uBugCheck();
        }
        p_out = "Analyse and write BPM to tags";
        return true;
    }
};

static contextmenu_item_factory_t<bpm_context_item> g_myitem_factory;

static void analyse_bpm(metadb_handle_list_cref data)
{
    bpm::run_calculate_bpm(data);
}
