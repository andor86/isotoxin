#include "isotoxin.h"

//-V:opts:807

namespace
{
    struct crallocator : public ts::dynamic_allocator_s
    {
        ts::block_buf_c< 512 * 1024 > texts;
        virtual void *balloc( size_t sz )
        {
            return texts.alloc( (uint)sz );
        }
        virtual void bfree( void *p )
        {
        }
    };
}

ts::asptr  calc_message_skin( message_type_app_e mt, const contact_key_s &sender )
{
    switch (mt)
    {
    case MTA_OLD_REQUEST:
        return CONSTASTR( "invite" );
    case MTA_ACCEPTED:
    case MTA_ACCEPT_OK:
        return CONSTASTR( "invite" );
    case MTA_RECV_FILE:
        return CONSTASTR( "filerecv" );
    case MTA_SEND_FILE:
        return CONSTASTR( "filesend" );
    case MTA_HIGHLIGHT_MESSAGE:
        return CONSTASTR( "pm" );
    }

    return sender.is_self ? CONSTASTR( "mine" ) : CONSTASTR( "other" );
}

void post_s::set_message_text( const ts::asptr&m )
{
    message_utf8 = ts::refstring_t<char>::build( m, g_app->global_allocator );
}


void avatar_s::load( const void *body, ts::aint size, int tag_ )
{
    alpha_pixels = false;
    ts::bitmap_c bmp;
    if (bmp.load_from_file(body, size))
    {
        if (tag_ == 0) ++tag; else tag = tag_;

        if (bmp.info().bytepp() != 4)
        {
            ts::bitmap_c bmp4;
            bmp4 = bmp.extbody();
            bmp = bmp4;
        }
        ts::ivec2 asz = parsevec2( gui->theme().conf().get_string(CONSTASTR("avatarsize")), ts::ivec2(32));
        if (bmp.info().sz != asz)
        {
            ts::bitmap_c bmprsz;

            if (bmp.info().sz.x != bmp.info().sz.y)
            {
                bmprsz.create_ARGB(ts::ivec2(ts::tmax(bmp.info().sz.x,bmp.info().sz.y)));
                bmprsz.fill(0);
                bmprsz.copy( (bmprsz.info().sz - bmp.info().sz) / 2, bmp.info().sz, bmp.extbody(), ts::ivec2(0) );
                bmp = bmprsz;
            }
            bmp.resize_to(bmprsz, asz, ts::FILTER_LANCZOS3);
            bmp = bmprsz;
        }

        if (bmp.is_alpha_blend())
        {
            alpha_pixels = true;
        } else
        {
            // apply mask
            if (const ts::image_extbody_c *mask = gui->theme().get_image(CONSTASTR("avamask")))
            {
                if (mask->info().sz >>= bmp.info().sz)
                {
                    if (mask->info().sz > bmp.info().sz)
                    {
                        ts::bitmap_c bmplarger;
                        bmplarger.create_ARGB(ts::tmax( mask->info().sz, bmp.info().sz ));
                        bmplarger.fill(0);
                        bmplarger.copy( (bmplarger.info().sz - bmp.info().sz) / 2, bmp.info().sz, bmp.extbody(), ts::ivec2(0) );
                        bmp = bmplarger;
                    }
                    bmp.before_modify();
                    ts::ivec2 offs = (bmp.info().sz - mask->info().sz) / 2;
                    ts::img_helper_copy_components(bmp.body() + 3 + offs.x * bmp.info().bytepp() + offs.y * bmp.info().pitch, mask->body() + 3, bmp.info(offs), mask->info(), 1);
                }
            }
            if (const ts::image_extbody_c *avabase = gui->theme().get_image(CONSTASTR("avabase")))
            {
                if (avabase->info().sz >>= bmp.info().sz)
                {
                    ts::bitmap_c prepared;
                    prepared.create_ARGB(avabase->info().sz);
                    ts::ivec2 offs = (avabase->info().sz - bmp.info().sz) / 2;
                    prepared.alpha_blend(offs, bmp.extbody(), avabase->extbody());
                    bmp = prepared;
                }
            }
        }

        *(ts::bitmap_c *)this = std::move(bmp);
        alpha_pixels = premultiply();
    }

}

ts::static_setup<contacts_c> contacts;

gmsg<ISOGM_MESSAGE>::gmsg(contact_c *sender, contact_c *receiver, message_type_app_e mt, uint64 utag) :gmsgbase(ISOGM_MESSAGE), sender(sender), receiver(receiver)
{
    post.utag = utag ? utag : prf().uniq_history_item_tag();
    post.recv_time = 0; // initialized after history add
    post.cr_time = 0;
    post.type = mt;
    post.sender = sender->getkey();
    post.receiver = receiver->getkey();
}

uint64 uniq_history_item_tag()
{
    return prf().uniq_history_item_tag();
}


contact_root_c *get_historian(contact_c *sender, contact_c * receiver)
{
    contact_root_c *historian;
    if (sender->getkey().is_self || receiver->getkey().is_conference())
    {
        // from self to other
        historian = receiver->get_historian();
    }
    else
    {
        historian = sender->get_historian();
    }
    return historian;
}

contact_c::contact_c( const contact_key_s &key ):key(key)
{
    options().unmasked().set(F_ALLOWACCEPTDATA); // see setup
}
contact_c::contact_c()
{
}
contact_c::~contact_c()
{
    if (gui)
    {
        gui->delete_event( DELEGATE(this,b_accept_call) );
    }
}

void contact_c::setmeta(contact_root_c *metac)
{
    ASSERT( dynamic_cast<contact_root_c *>(this) == nullptr );
    ASSERT(metac && metac->subpresent(key));
    metacontact = metac;
}

void contact_c::setup(const contacts_s * c, time_t nowtime)
{
    set_name(c->name);
    set_customname(c->customname);
    set_statusmsg(c->statusmsg);
    set_avatar(c->avatar.data(), c->avatar.size(), c->avatar_tag);
    options().setup(c->options);
    protodata = c->protodata;
    options().unmasked().init(F_ALLOWACCEPTDATA, protodata.size() == 0); // zero data? so, any data from protocol must be accepted

    if (opts.is(F_UNKNOWN))
        set_state(CS_UNKNOWN);
}

bool contact_c::save(contacts_s * c) const
{
    if (get_historian() == nullptr || getkey().temp_type != TCT_NONE)
        return false; // do not save unknown contacts from conferences

    c->metaid = getmeta()->getkey().contactid;
    c->options = get_options();
    c->name = get_name(false);
    c->customname = get_customname();
    c->statusmsg = get_statusmsg(false);
    c->protodata = protodata;
    // avatar data copied not here, see profile_c::set_avatar

    return true;
}


void contact_c::prepare4die(contact_root_c *owner)
{
    if (nullptr == owner || owner == metacontact)
    {
        opts.unmasked().set(F_DIP);
        metacontact = nullptr; // dec ref count
        if (contact_root_c *r = dynamic_cast<contact_root_c *>(this))
            r->subdelall();
    }
}

void contact_c::make_unknown_conference_member( int id )
{
    set_state( CS_UNKNOWN );
    contacts().change_key( key, contact_key_s( TCT_UNKNOWN_MEMBER, id, key.protoid ) );
}

void contact_c::redraw()
{
    if (contact_root_c *h = get_historian())
        if (h->gui_item)
            h->gui_item->getengine().redraw();
}

void contact_c::redraw(float delay)
{
    if (contact_root_c *h = get_historian())
        if (h->gui_item)
            h->gui_item->redraw(delay);
}

void contact_c::reselect(int options)
{
    if ( contact_root_c *h = get_historian() )
        g_app->reselect( h, options );
}

void contact_c::rebuild_system_user_avatar( active_protocol_c *ap )
{
    ASSERT( get_options().is(F_SYSTEM_USER) );
    ts::blob_c ablob;
    if ( !ap )
        ap = prf().ap( getkey().protoid );
    if (ap)
        ablob = ap->gen_system_user_avatar();
    set_avatar( ablob.data(), ablob.size(), 1 );
    prf().set_avatar( getkey(), ablob, 1 );
}

const avatar_s *contact_c::get_avatar() const
{
    if ( !getkey().is_conference() && getkey().contactid && avatar.get() == nullptr && prf_options().is( UIOPT_GEN_IDENTICONS ) )
        return g_app->gen_identicon_avatar( pubid );

    return avatar.get();
}

bool contact_c::b_accept_call_with_video(RID, GUIPARAM)
{
    accept_call( AAAC_NOT, true );
    return true;
}

bool contact_c::b_accept_call(RID, GUIPARAM prm)
{
    accept_call( (auto_accept_audio_call_e)as_int(prm), false );
    return true;
}

void contact_c::accept_call(auto_accept_audio_call_e aa, bool video)
{
    ASSERT( !is_rootcontact() && getmeta() != nullptr );

    if (aa)
    {
        // autoaccept
        play_sound(snd_call_accept, false);
    }

    if ( CHECK( getmeta() ) )
    {
        if ( aa || getmeta()->ringtone( this, false, false ) )
        {
            getmeta()->av( this, true, video );

            FPROTOCALL(accept_call);

            const post_s *p = get_historian()->fix_history( MTA_INCOMING_CALL, MTA_CALL_ACCEPTED, getkey(), get_historian()->nowtime() );
            if ( p ) gmsg<ISOGM_SUMMON_POST>( *p, true ).send();
        }

        if ( AAAC_ACCEPT_MUTE_MIC == aa )
        {
            if ( av_contact_s *avc = g_app->avcontacts().find_inprogress( avkey( getmeta()->getkey() ) ) )
                avc->mic_off();
        }
    }

    redraw();
}

bool contact_c::b_hangup(RID, GUIPARAM par)
{
    if ( is_meta() )
    {
        ts::ptr_cast<contact_root_c *>( this )->subiterate( [](contact_c *c) {
            if ( c->is_av() )
                c->b_hangup( RID(), nullptr );
        } );
        return true;
    }

    if (gui_notice_c *ownitm = (gui_notice_c *)par)
    {
        RID parent = HOLD( ownitm->getparent() )( ).getparent();
        gui->repos_children( &HOLD( parent ).as<gui_group_c>() );
        TSDEL( ownitm );
    }

    FPROTOCALL(stop_call);

    if (CHECK(getmeta()))
    {
        ts::str_c times;
        if (av_contact_s *avc = g_app->avcontacts().find_inprogress( avkey(getmeta()->getkey()) ))
        {
            int dt = static_cast<int>(ts::now() - avc->core->starttime);
            if (dt > 0)
                times = to_utf8( text_seconds(dt) );
        }

        const post_s *p = get_historian()->fix_history(MTA_CALL_ACCEPTED, MTA_HANGUP, getkey(), get_historian()->nowtime(), times.is_empty() ? nullptr : &times);
        if (p) gmsg<ISOGM_SUMMON_POST>(*p, true).send();
        getmeta()->av( this, false, false );
    }
    return true;
}

bool contact_c::b_call(RID, GUIPARAM is_video)
{
    ASSERT( !is_rootcontact() );
    if (!ASSERT(!is_av())) return true;

    FPROTOCALL(call, 60, is_video != nullptr);

    gmsg<ISOGM_NOTICE>( get_historian(), this, NOTICE_CALL ).send();
    get_historian()->calltone(this, is_video ? CALLTONE_VIDEO_CALL : CALLTONE_VOICE_CALL);

    return true;
}

bool contact_c::b_cancel_call(RID, GUIPARAM par)
{
    ASSERT( !is_rootcontact() );
    gui_notice_c *ownitm = (gui_notice_c *)par;
    RID parent = HOLD(ownitm->getparent())().getparent();
    gui->repos_children(&HOLD(parent).as<gui_group_c>());
    TSDEL(ownitm);

    if (get_historian()->calltone(this, CALLTONE_HANGUP))
        FPROTOCALL(stop_call);

    return true;
}

bool contact_c::b_reject_call(RID, GUIPARAM par)
{
    if (par)
    {
        gui_notice_c *ownitm = (gui_notice_c *)par;
        RID parent = HOLD(ownitm->getparent())().getparent();
        gui->repos_children(&HOLD(parent).as<gui_group_c>());
        TSDEL(ownitm);
    }

    if (CHECK(getmeta()))
    {
        if ( getmeta()->ringtone( this, false ) )
            FPROTOCALL(stop_call);

        const post_s *p = get_historian()->fix_history(MTA_INCOMING_CALL, MTA_INCOMING_CALL_REJECTED, getkey(), get_historian()->nowtime());
        if (p) gmsg<ISOGM_SUMMON_POST>( *p, true ).send();
    }

    return true;
}


bool contact_c::b_accept(RID, GUIPARAM par)
{
    if (par)
    {
        gui_notice_c *ownitm = (gui_notice_c *)par;
        RID parent = HOLD(ownitm->getparent())().getparent();
        gui->repos_children(&HOLD(parent).as<gui_group_c>());
        TSDEL(ownitm);
    }

    FPROTOCALL(accept);

    opts.unmasked().set(F_JUST_ACCEPTED);

    get_historian()->fix_history(MTA_FRIEND_REQUEST, MTA_OLD_REQUEST, getkey());
    get_historian()->reselect(0);

    prf().dirtycontact(getkey());
    return true;
}
bool contact_c::b_reject(RID, GUIPARAM par)
{
    FPROTOCALL( reject );

    contacts().kill( get_historian()->getkey() );

    return true;
}

bool contact_c::b_resend(RID, GUIPARAM)
{
    SUMMON_DIALOG<dialog_addcontact_c>(UD_ADDCONTACT, true, dialog_addcontact_params_s( gui_isodialog_c::title(title_repeat_request), get_pubid(), getkey() ));
    return true;
}

bool contact_c::b_kill(RID, GUIPARAM)
{
    contacts().kill( getkey() );
    return true;
}

bool contact_c::b_receive_file(RID, GUIPARAM par)
{
    gui_notice_c *ownitm = (gui_notice_c *)par;
    uint64 utag = ownitm->get_utag();

    if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(utag))
    {
        if (ft->auto_confirm())
        {
            RID parent = HOLD(ownitm->getparent())().getparent();
            gui->repos_children(&HOLD(parent).as<gui_group_c>());
            TSDEL(ownitm);
        } else
        {
            ts::master().sys_beep(ts::SBEEP_ERROR);
        }
    }

    return true;
}

bool contact_c::b_receive_file_as(RID, GUIPARAM par)
{
    gui_notice_c *ownitm = (gui_notice_c *)par;
    uint64 utag = ownitm->get_utag();

    if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(utag))
    {
        ts::wstr_c downf = prf().download_folder();
        path_expand_env(downf, get_historian()->contactidfolder());
        ts::make_path(downf, 0);

        ts::wstr_c title(TTT("Save file",179));
        ts::extensions_s exts;
        ts::wstr_c fn = ownitm->getroot()->save_filename_dialog(downf, ft->filename, exts, title);

        if (!fn.is_empty())
        {
            uint64 avaspace = ts::get_free_space( ts::fn_get_path( fn ) );
            if ( ft->filesize > avaspace )
            {
                struct handlers
                {
                    ts::safe_ptr<gui_notice_c> ownitm;
                    uint64 utag;
                    ts::wstr_c fn;

                    handlers( gui_notice_c * ownitm, uint64 utag, const ts::wstr_c &fn ) :ownitm( ownitm ), utag( utag ), fn(fn) {}
                    void m_continue( const ts::str_c&cks )
                    {
                        if ( ownitm )
                            if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag( utag ))
                            {
                                RID parent = HOLD( ownitm->getparent() )().getparent();
                                gui->repos_children( &HOLD( parent ).as<gui_group_c>() );
                                TSDEL( ownitm );

                                ft->prepare_fn( fn, true );
                                if (active_protocol_c *ap = prf().ap( ft->sender.protoid ))
                                    ap->file_accept( ft->i_utag, 0 );
                            }
                    }
                    void m_cancel( const ts::str_c&cks )
                    {
                        TSDEL( this );
                    }
                } *h = TSNEW( handlers, ownitm, utag, fn );

                ts::wstr_c txt = TTT("Not enough space for file[br]$[br]File size: $[br]Available space: $",514) / ts::wstr_c(CONSTWSTR("<b>"), fn, CONSTWSTR("</b>")) / text_sizebytes(ft->filesize, true) / text_sizebytes( avaspace, true );
                dialog_msgbox_c::mb_warning( txt )
                    .bcancel().on_cancel( DELEGATE( h, m_cancel ), ts::str_c() )
                    .bok( loc_text(loc_continue) ).on_ok( DELEGATE( h, m_continue ), ts::str_c() )
                    .summon(true);
            } else
            {
                RID parent = HOLD( ownitm->getparent() )().getparent();
                gui->repos_children( &HOLD( parent ).as<gui_group_c>() );
                TSDEL( ownitm );

                ft->prepare_fn( fn, true );
                if (active_protocol_c *ap = prf().ap( ft->sender.protoid ))
                    ap->file_accept( ft->i_utag, 0 );
            }

        }
    }

    return true;
}

bool contact_c::b_refuse_file(RID, GUIPARAM par)
{
    gui_notice_c *ownitm = (gui_notice_c *)par;
    uint64 utag = ownitm->get_utag();
    RID parent = HOLD(ownitm->getparent())().getparent();
    gui->repos_children(&HOLD(parent).as<gui_group_c>());
    TSDEL(ownitm);

    if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(utag))
    {
        ASSERT( getkey() == ft->sender );
        ft->kill( FIC_REJECT );
    }

    return true;
}


bool contact_c::set_protodata(const ts::blob_c &d)
{
    if (options().unmasked().is(F_ALLOWACCEPTDATA) && d != protodata)
    {
        // accept only of data already sent to protocol
        protodata = d;
        return true;
    }
    return false;
}

void contact_c::detach()
{
    if (getmeta())
    {
        ts::shared_ptr<contact_c> c = this;
        ts::db_transaction_c __transaction(prf().get_db());

        contact_root_c *historian = c->get_historian();
        prf().load_history(historian->getkey()); // load whole history into memory table
        historian->unload_history(); // clear history cache in contact
        contact_root_c *detached_meta = contacts().create_new_meta();
        historian->subremove(c);
        detached_meta->subadd(c);
        if (historian->gui_item) historian->gui_item->update_text();
        prf().dirtycontact(c->getkey());
        prf().detach_history(historian->getkey(), detached_meta->getkey(), c->getkey());
        gmsg<ISOGM_V_UPDATE_CONTACT>(c, true).send();
        gmsg<ISOGM_UPDATE_CONTACT> upd;
        upd.key = contact_id_s(contact_id_s::CONTACT, c->getkey().contactid);
        upd.apid = c->getkey().protoid;
        upd.mask = CDM_STATE;
        upd.state = c->get_state();
        upd.send();
        historian->reselect();
        prf().flush_contacts();
        while (prf().flush_tables());
    }

}

contacts_c::contacts_c()
{
    self = TSNEW(contact_root_c, contact_key_s(true));
    arr.add(self.get());
    resort_list();
}
contacts_c::~contacts_c()
{
    for(contact_c *c : arr)
        c->prepare4die(nullptr);
}

ts::str_c contacts_c::find_pubid(int protoid) const
{
    if (contact_c *sc = find_subself(protoid))
        return sc->get_pubid();

    return ts::str_c();
}
contact_c *contacts_c::find_subself(int protoid) const
{
    return self->subget( contact_key_s(contact_id_s::make_self(), protoid) );
}

ts::wstr_c contacts_c::calc_self_name( int protoid ) const
{
    if ( active_protocol_c *ap = prf().ap( protoid ) )
    {
        ts::str_c n( ap->get_actual_username() );
        text_adapt_user_input( n );
        text_remove_tags( n );
        return from_utf8( n );
    }

    return ts::wstr_c();
}

void contacts_c::nomore_proto(int id)
{
    ts::db_transaction_c __transaction(prf().get_db());

    check_again:
    for (ts::aint i = arr.size() - 1; i >= 0; --i)
    {
        contact_c *c = arr.get(i);
        if (c->getkey().protoid == (unsigned)id && (c->getkey().is_conference() || !c->is_meta()))
            if (c->getmeta() && c->getmeta()->subcount() > 1)
            {
                // split meta
                c->detach();
                goto check_again;
            }
    }

    ts::tmp_array_inplace_t<contact_key_s, 16> c2d;
    for( ts::aint i = arr.size() - 1; i>=0 ;--i )
    {
        contact_c *c = arr.get(i);
        if (c->getkey().protoid == (unsigned)id && ( c->getkey().is_conference() || !c->is_meta()))
            c2d.add(c->getkey());
    }
    for (const contact_key_s &ck : c2d)
    {
        if (contact_c *c = find(ck))
            if (c->get_historian()->gui_item == g_app->active_contact_item)
            {
                contacts().get_self().reselect();
                break;
            }
    }
    for( const contact_key_s &ck : c2d )
        kill(ck, true);

    resort_list();
}

bool contacts_c::present_protoid(int id) const
{
    for(const contact_c *c : arr)
        if (!c->is_meta() && c->getkey().protoid == (unsigned)id)
            return true;
    return false;
}

int contacts_c::find_free_meta_id() const
{
    prf().get_table_contacts().cleanup();
    int id=1;
    for(; present( contact_key_s(id) ) || !prf().isfreecontact(contact_key_s(id))  ;++id) ;
    return id;
}

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_CHANGED_SETTINGS>&ch)
{
    if (ch.pass == 0)
    {
        switch (ch.sp)
        {
        case PP_USERNAME:
            get_self().set_name(ch.s);
            break;
        case PP_USERSTATUSMSG:
            get_self().set_statusmsg(ch.s);
            break;
        }
    }

    return 0;
}

ts::uint32 contacts_c::gm_handler( gmsg<ISOGM_PROTO_CRASHED>&crashed )
{
    bool resort = false;
    for( contact_c *c : arr )
    {
        if ( c->getkey().is_conference() || !c->is_meta() )
        {
            if ( c->get_state() == CS_ONLINE && c->getkey().protoid == (unsigned)crashed.id )
            {
                if ( c->is_av() )
                {
                    c->get_historian()->stop_av();
                    gmsg<ISOGM_NOTICE>( nullptr, nullptr, NOTICE_KILL_CALL_INPROGRESS ).send();
                }

                c->set_state( CS_OFFLINE );
                resort = true;
            }
        }
    }

    prf().get_table_conference().cleanup();
    prf().get_table_conference_member().cleanup();

    for( auto &row : prf().get_table_conference() )
        if ( row.other.proto_id == (unsigned)crashed.id )
            row.other.proto_key = contact_id_s();

    for ( auto &row : prf().get_table_conference_member() )
        if ( row.other.proto_id == (unsigned)crashed.id )
            row.other.proto_key = contact_id_s();

    if ( resort )
    {
        play_sound( snd_friend_offline, false );
        resort_list();
    }

    return 0;
}

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_PROTO_LOADED>&l)
{
    if (active_protocol_c *ap = prf().ap(l.id))
    {
        for (contact_c *c : arr)
        {
            if (!c->getkey().is_conference() && !c->is_meta())
            {
                if (c->getkey().protoid == (unsigned)l.id)
                {
                    ap->send_proto_data(c->getkey().gidcid(), c->get_protodata());
                    c->options().unmasked().set( contact_c::F_ALLOWACCEPTDATA );
                }
            }
        }
    }
    return 0;
}

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_PROFILE_TABLE_SL>&msg)
{
    if (msg.saved)
        return 0;

    if (msg.tabi == pt_contacts)
    {
        time_t nowtime = ts::now();
        // 1st pass - create meta
        typedef tableview_t<contacts_s, pt_contacts>::row_s trow;
        ts::tmp_pointers_t<trow, 32> notmeta;
        ts::tmp_pointers_t<contact_root_c, 1> corrupt;
        for( auto &row : prf().get_table_contacts() )
        {
            if (row.other.metaid == 0)
            {
                contact_key_s metakey( contact_id_s(contact_id_s::CONTACT, row.other.key.contactid), row.other.key.protoid); // row.other.key.protoid == 0 for meta and != 0 for unknown

                contact_root_c *metac = nullptr;
                ts::aint index;
                if (arr.find_sorted(index, metakey))
                    metac = ts::ptr_cast<contact_root_c *>(arr.get(index).get());
                else
                {
                    metac = TSNEW(contact_root_c, metakey);
                    arr.insert(index, metac);
                }
                metac->setup(&row.other, nowtime);

                if( metac->getkey().protoid == 0 || metac->getkey().is_conference() || metac->get_state() == CS_UNKNOWN )
                {
                    // ok
                } else
                {
                    // corrupt?
                    // contact lost its historian meta
                    corrupt.add( metac );
                }

            } else
                notmeta.add(&row);
        }

        // detect doubles
        bool changed_ctable = false;
        notmeta.sort( []( const trow *c1, const trow *c2 )->bool {
            if (c1->other.key == c2->other.key)
                return c1->other.metaid < c2->other.metaid;
            return c1->other.key < c2->other.key;
        } );
        for( ts::aint i=notmeta.size() - 2;i>=0;--i)
        {
            trow *c1 = notmeta.get(i);
            trow *c2 = notmeta.get(i+1);
            if (c1->other.key == c2->other.key)
            {
                if (c1->other.metaid == c2->other.metaid)
                {
                    trow *keep = c2;
                    trow *del = c1;
                    if (c1->id < c2->id)
                    {
                        keep = c1;
                        del = c2;
                        notmeta.remove_slow(i + 1);
                    }
                    else
                        notmeta.remove_slow(i);

                    del->deleted();
                    keep->other.avatar_tag = 0; // reload avatar
                    keep->changed();
                    changed_ctable = true;
                } else
                {
                    ts::aint index1, index2;
                    bool c1_meta_present = arr.find_sorted(index1, contact_key_s(c1->other.metaid));
                    bool c2_meta_present = arr.find_sorted(index2, contact_key_s(c2->other.metaid));
                    if (c1_meta_present)
                        c1_meta_present = prf().calc_history( arr.get(index1)->getkey() ) > 0;
                    if (c2_meta_present)
                        c2_meta_present = prf().calc_history(arr.get(index2)->getkey()) > 0;

                    trow *keep = c2;
                    trow *del = c1;
                    if ((c1->id < c2->id && c1_meta_present == c2_meta_present) || !c2_meta_present)
                    {
                        keep = c1;
                        del = c2;
                        notmeta.remove_slow(i + 1);
                    }
                    else
                        notmeta.remove_slow(i);

                    del->deleted();
                    keep->other.avatar_tag = 0; // reload avatar
                    keep->changed();
                    changed_ctable = true;


                }
            }
        }
        if (changed_ctable)
            prf().changed();

        for(trow *trowc : notmeta)
        {
            contacts_s *c = &trowc->other;
            contact_root_c *metac = nullptr;
            ts::aint index;
            if (CHECK(arr.find_sorted(index, contact_key_s(c->metaid))))
            {
                metac = ts::ptr_cast<contact_root_c *>( arr.get(index).get() );
            meta_restored:
                contact_c *cc = metac->subgetadd(c->key);
                cc->setup(c, 0);

                if (!arr.find_sorted(index, cc->getkey()))
                    arr.insert(index, cc);
            } else
            {
                contact_key_s metakey(c->metaid);
                metac = TSNEW(contact_root_c, metakey);
                arr.insert(index, metac);
                prf().dirtycontact(metac->getkey());
                goto meta_restored;
            }
        }

        if (ts::aint sz = corrupt.size())
        {
            // try to fix corruption
            for(contact_c *c : arr)
                if (c->is_meta())
                    if (contact_root_c *r = ts::ptr_cast<contact_root_c *>(c))
                        if (r->subcount() == 0)
                            corrupt.add(r); // empty meta? may be it is lost historian?

            while( corrupt.size() )
            {
                contact_c *c = corrupt.get(0);

                if (corrupt.size() == sz)
                {
                    // no empty historians
                    contact_root_c *meta = create_new_meta();
                    meta->subadd(c);
                    prf().dirtycontact(c->getkey());
                }
                else
                {
                    contact_root_c *historian = prf().find_corresponding_historian( c->getkey(), corrupt.array().subarray(sz) );
                    if (historian)
                    {
                        corrupt.find_remove_fast( historian );
                        historian->subadd(c);
                        prf().dirtycontact(c->getkey());
                    }
                }

                corrupt.remove_slow(0);
                --sz;
            }
        } else
        {
            for (contact_c *c : arr)
                if (c->is_meta())
                    if (contact_root_c *r = ts::ptr_cast<contact_root_c *>(c))
                        if (r->subcount() == 0)
                            corrupt.add(r); // empty meta? may be it is lost historian?

            while (corrupt.size())
            {
                contact_root_c *c = corrupt.get_last_remove();
                prf().killcontact(c->getkey());
                delbykey(c->getkey());
            }
        }

        prf().get_table_conference().cleanup();
        prf().get_table_conference_member().cleanup();

        // restore conferences
        for ( auto &row : prf().get_table_conference() )
        {
            contact_root_c *g = TSNEW( contact_root_c, contact_key_s( TCT_CONFERENCE, row.other.id, row.other.proto_id ) );
            row.other.confa = g;

            ts::aint index;
            if ( CHECK( !arr.find_sorted( index, g->getkey() ) ) )
                arr.insert( index, g );

            g->set_pubid( row.other.pubid );
            g->set_name( row.other.name );
            g->set_readtime( row.other.readtime );
            g->set_state( CS_OFFLINE );
            g->options().unmasked().set( contact_c::F_HISTORY_NEED_LOAD );
            g->set_imnb( row.other.flags.is(conference_s::F_SUPPRESS_NOTIFICATIONS) ? IMB_DONT_NOTIFY : IMB_DEFAULT );
            g->set_comment( row.other.comment );
            g->set_tags( row.other.tags );
            g->set_keeph( static_cast<keep_contact_history_e>(row.other.flags >> 16) );
        }

        // restore unknown contacts
        for ( auto &row : prf().get_table_conference_member() )
        {
            contact_c *uc = TSNEW( contact_c, contact_key_s( TCT_UNKNOWN_MEMBER, row.other.id, row.other.proto_id ) );
            row.other.memba = uc;

            ts::aint index;
            if ( CHECK( !arr.find_sorted( index, uc->getkey() ) ) )
                arr.insert( index, uc );

            uc->set_pubid( row.other.pubid );
            uc->set_name( row.other.name );
            uc->set_state( CS_UNKNOWN );
        }

        contact_online_state_e cos_ = (contact_online_state_e)prf().manual_cos();
        if (cos_ != COS_ONLINE)
            g_app->set_status( cos_, true );

        rebuild_tags_bits();

        iterate_proto_contacts([&](contact_c *c) {

            if (!c->getkey().is_conference() && c->getkey().temp_type == TCT_NONE)
                g_app->setuap(c->getkey().protoid, c->getkey().contactid);
            return true;
        });

        return 0;
    }

    if (msg.tabi == pt_unfinished_file_transfer)
    {
        // cleanup transfers
        TS_STATIC_CHECK( pt_unfinished_file_transfer > pt_contacts, "unfinished transfer table must be loaded after contacts" );
        while( auto *row =  prf().get_table_unfinished_file_transfer().find<true>([](unfinished_file_transfer_s &uft){

            if (0 == uft.msgitem_utag) return true;
            contact_c *c = contacts().find( uft.historian );
            if (c == nullptr) return true;
            prf().load_history( c->getkey() );
            if (nullptr == prf().get_table_history().find<true>([&](history_s &h){
                if (h.historian != uft.historian) return false;
                if (uft.msgitem_utag == h.utag) return true;
                return false;
            }))
                return true;

            return false;
        })) { if (row->deleted()) prf().changed(); }

        return 0;
    }

    if (msg.tabi == pt_active_protocol)
        g_app->recreate_ctls(true, true);

    return 0;
}

void contacts_c::update_roots()
{
    for ( contact_c *c : arr )
        if (c->is_rootcontact() && !c->getkey().is_self)
            gmsg<ISOGM_V_UPDATE_CONTACT>(c, true).send();
}

bool contacts_c::is_conference_member( const contact_key_s &ck, bool strong_check )
{
    if (ck.is_conference())
        return false;

    bool yep = false;
    for (contact_c *c : arr)
        if (c->getkey().is_conference() && ts::ptr_cast<contact_root_c *>(c)->subpresent(ck))
        {
            c->redraw(1.0f);
            yep = true;
            gmsg<ISOGM_V_UPDATE_CONTACT>( c, false ).send();
        }
    return yep || (strong_check ? prf().is_conference_member(ck) : false);
}

void contacts_c::change_key( const contact_key_s &oldkey, const contact_key_s &newkey )
{
    ts::aint index;
    if (arr.find_sorted(index,oldkey))
    {
        ts::shared_ptr<contact_c> keeper( arr.get(index) );
        arr.remove_slow(index);
        keeper->key = newkey;
        arr.find_sorted( index, newkey );
        arr.insert( index, keeper);
    }
}

void contacts_c::kill(const contact_key_s &ck, bool kill_with_history)
{
    contact_c * cc = find(ck);
    if (!cc) return;

    cc->get_historian()->stop_av();

    ts::safe_ptr<gui_contact_item_c> guiitem = cc->get_historian()->gui_item;
    bool selself = !guiitem.expired() && g_app->active_contact_item.get() == guiitem.get();

    if ( cc->getkey().is_conference() && cc->get_state() == CS_OFFLINE )
    {
        ASSERT( cc->getkey().temp_type == TCT_CONFERENCE );

        cc->set_state( CS_ROTTEN );
        gmsg<ISOGM_V_UPDATE_CONTACT>( cc, true ).send(); // delete ui element

        if ( guiitem )
            TSDEL( guiitem );

        prf().delete_conference( cc->getkey().contactid );
        prf().kill_history( cc->getkey() );

        ts::aint index;
        if ( arr.find_sorted( index, cc->getkey() ) )
            arr.remove_slow( index );

        if ( selself )
            contacts().get_self().reselect();

        resort_list();
        return;
    }

    if (cc->get_state() != CS_UNKNOWN)
    {
        cc->set_state(CS_ROTTEN);
        gmsg<ISOGM_V_UPDATE_CONTACT>(cc, true).send(); // delete ui element
    }

    bool killcc = false;
    if ( cc->is_meta() && !cc->getkey().is_conference() )
    {
        contact_root_c *ccc = ts::ptr_cast<contact_root_c *>(cc);
        ccc->subiterate([this](contact_c *c) {

            contact_key_s k = c->getkey();

            if (is_conference_member( c->getkey(), true ))
            {
                prf().make_contact_unknown_member( c );
            } else
            {
                prf().killcontact(c->getkey());
                delbykey(c->getkey());
            }

            if ( active_protocol_c *ap = prf().ap( k.protoid ) )
                ap->del_contact( k );

        });
        prf().kill_history( cc->getkey() );
        killcc = true;
    } else
    {
        if (contact_root_c *meta = cc->getmeta())
        {
            int live = 0;
            bool historian_killed_too = false;
            if (kill_with_history || prf().calc_history(meta->getkey()) == 0)
            {
                meta->subiterate([&](contact_c *c) {
                    if (c->get_state() != CS_ROTTEN) ++live;
                });
                if (live == 0)
                {
                    killmeta:
                    meta->set_state(CS_ROTTEN);
                    gmsg<ISOGM_V_UPDATE_CONTACT>(meta, true).send();
                    prf().killcontact(meta->getkey());
                    prf().kill_history(meta->getkey());
                    delbykey(meta->getkey());
                    g_app->cancel_file_transfers( meta->getkey() );
                    historian_killed_too = true;
                }
            } else
            {
                if ( meta->subremove( cc ) ) goto killmeta;
            }
            if (!historian_killed_too)
                guiitem = nullptr, selself = false; // avoid kill gui item

        } else
        {
            ASSERT(cc->getkey().is_conference() || cc->get_state() == CS_UNKNOWN);
        }

        if (active_protocol_c *ap = prf().ap(cc->getkey().protoid))
            ap->del_contact(cc->getkey());

        if ( cc->getkey().temp_type == TCT_CONFERENCE )
        {
            prf().delete_conference( cc->getkey().contactid );
            prf().kill_history( cc->getkey() );
        }

        if ( is_conference_member( cc->getkey(), true ) )
        {
            prf().make_contact_unknown_member( cc );
            killcc = false;
        } else
            killcc = true;
    }

    if (killcc)
    {
        prf().killcontact(cc->getkey());
        delbykey(cc->getkey());
    }

    if (guiitem)
        TSDEL(guiitem);

    if (selself)
        contacts().get_self().reselect();
}

contact_root_c *contacts_c::create_new_meta()
{
    contact_root_c *metac = TSNEW(contact_root_c, contact_key_s(find_free_meta_id()));

    ts::aint index;
    if (CHECK(!arr.find_sorted(index, metac->getkey())))
        arr.insert(index, metac);

    prf().kill_history(metac->getkey() );
    prf().dirtycontact(metac->getkey());

    return metac;
}

ts::uint32 contacts_c::gm_handler(gmsg<GM_UI_EVENT> &e)
{
    if (UE_THEMECHANGED == e.evt)
    {
        prf().get_table_contacts().cleanup();
        for(contact_c *c : arr)
        {
            if (c->getmeta() != nullptr && !c->getkey().is_self)
            {
                if (const avatar_s *a = c->get_avatar())
                {
                    ts::blob_c ava = prf().get_avatar(c->getkey());
                    c->set_avatar(ava.data(), ava.size(), ava.size() ? a->tag : 0);
                }
            }
        }
    }
    return 0;
}

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_AVATAR> &ava)
{
    if (contact_c *c = find(ava.contact))
    {
        UNFINISHED( "animated gif support" );

        ts::bitmap_c avapic;
        ts::img_format_e fmt = avapic.load_from_file( ava.data );
        if ( ts::if_none == fmt )
            ava.data.clear();
        else if ( ts::if_png != fmt )
        {
            ts::buf_c b;
            avapic.save_as_png(b);
            ava.data.clear();
            ava.data.append_buf(b);
        }

        c->set_avatar( ava.data.data(), ava.data.size(), ava.tag );

        if ( prf().misc_flags() & PMISCF_SAVEAVATARS )
        {
            ts::wstr_c downf = prf().download_folder_images();
            path_expand_env( downf, c->get_historian()->contactidfolder() );
            ts::make_path( downf, 0 );
            if ( ava.data.size() && ts::dir_present( downf ) )
                ava.data.save_to_file( ts::fn_join( downf, CONSTWSTR( "avatar_" ) ).append_as_uint( ava.tag ).append( CONSTWSTR( ".png" ) ) );
        }
    }

    prf().set_avatar( ava.contact, ava.data, ava.tag );

    return 0;
}

ts::uint32 contacts_c::gm_handler( gmsg<ISOGM_UPDATE_CONTACT>&contact )
{
    MEMT( MEMT_CONTACTS );

    resort_list();

    int serious_change = 0;
    if (contact.state == CS_ROTTEN && 0 != (contact.mask & CDM_STATE))
    {
        kill( contact_key_s(contact.key, contact.apid) );
        return 0;
    }

    contact_c *c_sub = nullptr;
    contact_root_c *c_confa = nullptr;
    conference_member_s *cm = nullptr;
    conference_s *c = nullptr;

    bool is_self = false;
    bool refilter = false;

    if (contact.key.is_self())
    {
        c_sub = self->subgetadd(contact_key_s(contact_id_s::make_self(), contact.apid));
        if (active_protocol_c *ap = prf().ap(contact.apid))
        {
            bool changed = ap->set_current_online( contact.state == CS_ONLINE );
            ap->set_avatar(c_sub);

            if ( g_app->update_state() || changed )
            {
                g_app->set_notification_icon();
                ts::irect ir(0,0,64,64);
                HOLD(g_app->main).engine().redraw( &ir );

                iterate_proto_contacts( [&]( contact_c *c )->bool {
                    if ( c->get_options().is( contact_c::F_SYSTEM_USER ) && c->getkey().protoid == contact.apid )
                        c->rebuild_system_user_avatar(ap);
                    return true;
                } );
            }
        }
        is_self = true;

    } else
    {
        ts::aint index = -1;

        if ( contact.key.is_conference() )
        {
            ASSERT( !arr.find_sorted( index, contact_key_s(contact.key, contact.apid)) );

            auto calc_conferences_amount = [this]()->int
            {
                int n = 0;
                for ( contact_c *c : arr )
                    if ( c->getkey().is_conference() )
                        ++n;
                return n;
            };

            if ( 0 != ( contact.mask & CDM_PUBID ) )
                c = prf().find_conference( contact.pubid, contact.apid );
            else
                c = prf().find_conference( contact.key, contact.apid );

            bool just_connected = false;

            if (c)
            {
                ASSERT( c->confa );

                if ( c->proto_key.is_empty() )
                    c->proto_key = contact.key, just_connected = true;
                else
                {
                    ASSERT( c->proto_key == contact.key );
                }
            } else
            {
                // 1st time

                ASSERT( ( 0 != ( contact.mask & CDM_PUBID ) ) || contact.pubid.is_empty() );
                c = prf().add_conference( contact.pubid, contact.key, contact.apid );

                ASSERT( c->proto_key == contact.key && contact.apid );

                c->confa = TSNEW( contact_root_c, c->history_key() );

                if ( CHECK( !arr.find_sorted( index, c->confa->getkey() ) ) )
                {
                    arr.insert( index, c->confa.get() );
                    prf().purifycontact( c->confa->getkey() );
                    serious_change = contact.apid;
                }

                just_connected = true;
            }

            c_confa = c->confa;

            bool audio = contact.key.audio != 0;
            if ( c_confa->is_av() != audio )
            {
                c_confa->av( nullptr, audio, false );
                refilter = true;
            }

            if (0 != (contact.mask & CDM_PERMISSIONS))
                c_confa->set_conference_permissions( contact.grants );

            if (0 != (contact.mask & CDM_DETAILS))
                c_confa->set_details( contact.details );

            if ( 0 != ( contact.mask & CDM_NAME ) )
            {
                bool name_changed = false;

                if ( !contact.name.is_empty() )
                    c_confa->set_name( contact.name ), name_changed = true;
                else if ( c_confa->get_name().is_empty() )
                    c_confa->set_name( CONSTASTR( "Conference #" ) + ts::amake<int>( calc_conferences_amount() ) ), name_changed = true;

                if (name_changed)
                    c->update_name();

                contact.mask &= ~CDM_NAME;
            }

            if ( 0 != ( contact.mask & CDM_MEMBERS ) )
            {
                ts::aint wasmembers = c_confa->subcount();
                c_confa->subclear();
                for ( contact_id_s cid : contact.members )
                {
                    contact_key_s mk( cid, c_confa->getkey().protoid );
                    if (contact_c *m = find( mk ))
                    {
                        ASSERT( m->get_state() != CS_UNKNOWN );
                        c_confa->subaddconference( m );

                    } else if (conference_member_s *tcm = prf().find_conference_member( cid, c_confa->getkey().protoid ))
                        c_confa->subaddconference( tcm->memba );
                }

                if ( wasmembers == 0 && c_confa->subcount() )
                    play_sound( snd_call_accept, false ), contact_activity(c_confa->getkey());
                if ( wasmembers > 0 && !c_confa->subcount() )
                    play_sound( snd_hangup, false );

                if ( c_confa->subcount() && c_confa->is_av() )
                    c_confa->av(nullptr, true, false); // update av for members

                refilter |= wasmembers != c_confa->subcount();
                contact.mask &= ~CDM_MEMBERS;
            }


            refilter |= just_connected;

        } else if ( contact.key.confmember )
        {
            ASSERT( !arr.find_sorted( index, contact_key_s(contact.key, contact.apid)) );

            if ( 0 != ( contact.mask & CDM_PUBID ) )
                cm = prf().find_conference_member( contact.pubid, contact.apid );
            else
                cm = prf().find_conference_member( contact.key, contact.apid );

            if ( cm )
            {
            process_update_as_unknown_conference_member:

                ASSERT( cm->memba );

                if ( cm->proto_key.is_empty() )
                    cm->proto_key = contact.key;
                else
                {
                    ASSERT( cm->proto_key == contact.key );
                }
            }
            else
            {
                // 1st time

                ASSERT( 0 != ( contact.mask & CDM_PUBID ) );
                cm = prf().add_conference_member( contact.pubid, contact.key, contact.apid );

                ASSERT( cm->proto_key == contact.key && contact.apid );

                cm->memba = TSNEW( contact_c, cm->history_key() );
                cm->memba->set_state(CS_UNKNOWN);

                if ( CHECK( !arr.find_sorted( index, cm->memba->getkey() ) ) )
                {
                    arr.insert( index, cm->memba );
                    prf().purifycontact( cm->memba->getkey() );
                    serious_change = contact.apid;
                }
            }

            c_sub = cm->memba;

        } else if (!arr.find_sorted(index, contact_key_s(contact.key, contact.apid)))
        {
            if ( 0 != ( contact.mask & CDM_PUBID ) )
                if ( nullptr != (cm = prf().find_conference_member( contact.pubid, contact.apid )) )
                {
                    if ( 0 != ( contact.mask & CDM_STATE ) )
                    {
                        if ( contact.state == CS_UNKNOWN )
                            goto process_update_as_unknown_conference_member;

                        // looks like unknown contact is about to become known
                        c_sub = cm->memba;
                        ASSERT( c_sub && !c_sub->getmeta() );

                        prf().make_contact_known( c_sub, contact_key_s(contact.key, contact.apid) );
                        create_new_meta()->subadd( c_sub );

                        prf().delete_conference_member( cm->id );
                        serious_change = contact.apid;
                        refilter = true;
                    }

                }

            if ( c_sub == nullptr )
            {
                if ( 0 != ( contact.mask & CDM_STATE ) && contact.state == CS_UNKNOWN )
                {
                    // non-authorized contact, not conference member

                    c_sub = TSNEW( contact_c, contact_key_s(contact.key, contact.apid) );
                    c_sub->set_state( CS_UNKNOWN );
                    arr.insert( index, c_sub );

                    contact_root_c *h = create_new_meta();
                    h->subadd( c_sub );
                    refilter = true;

                    if (contact.key.allowinvite )
                        c_sub->options().set( contact_c::F_ALLOW_INVITE );

                    prf().purifycontact( c_sub->getkey() );
                    serious_change = contact.apid;

                }
                else
                {
                    c_sub = create_new_meta()->subgetadd(contact_key_s(contact.key, contact.apid));

                    if ( CHECK( !arr.find_sorted( index, c_sub->getkey() ) ) ) // find index again due create_new_meta can make current index invalid
                        arr.insert( index, c_sub );

                    prf().purifycontact( c_sub->getkey() );

                    // serious change - new contact added. proto data must be saved
                    serious_change = c_sub->getkey().protoid;
                    refilter = true;
                }
            }

        } else
        {
            c_sub = arr.get(index);
            if (c_sub->getkey().is_conference())
            {
                c_confa = ts::ptr_cast<contact_root_c *>(c_sub);
                c_sub = nullptr;

            } else if (c_sub->get_options().is(contact_c::F_UNKNOWN))
            {

            } else
            {
                ASSERT(c_sub->getmeta() != nullptr); // it must be part of metacontact
            }
        }
    }

    if ( contact.key.sysuser && c_sub )
    {
        contact.state = CS_OFFLINE; // to keep this contact in db
        c_sub->options().clear( contact_c::F_ALLOW_INVITE );
        c_sub->get_historian()->options().set( contact_c::F_SYSTEM_USER );

        active_protocol_c *ap = prf().ap( c_sub->getkey().protoid );
        if (ap) c_sub->set_name( ap->get_name() );

        if ( !c_sub->get_options().is( contact_c::F_SYSTEM_USER ) || c_sub->avatar_tag() != 1 )
        {
            c_sub->options().set( contact_c::F_SYSTEM_USER );
            c_sub->rebuild_system_user_avatar(ap);
        }
        RESETFLAG( contact.mask, CDM_AVATAR_TAG );
        RESETFLAG( contact.mask, CDM_NAME );
    }

    if (0 != (contact.mask & CDM_PUBID))
    {
        if ( c_sub )
        {
            // allow change pubid if
            // - not yet set
            // - of self
            // - just accepted contact (Tox: because send request pubid and accepted pubid are not same (see nospam tail) )

            bool accepted = c_sub->get_state() == CS_INVITE_SEND && ( 0 != ( contact.mask & CDM_STATE ) ) && ( contact.state == CS_ONLINE || contact.state == CS_OFFLINE );
            if ( !accepted ) accepted = c_sub->get_state() == CS_INVITE_RECEIVE && ( 0 != ( contact.mask & CDM_STATE ) ) && ( contact.state == CS_INVITE_SEND || contact.state == CS_ONLINE || contact.state == CS_OFFLINE );
            if ( c_sub->get_pubid().is_empty() || is_self || accepted )
                c_sub->set_pubid( contact.pubid );
            else
            {
                ASSERT( c_sub->get_pubid().equals( contact.pubid ) );
            }
        } else if ( c_confa )
        {
             c_confa->set_pubid( contact.pubid );
        }
    }

    if (0 != (contact.mask & CDM_NAME))
    {
        if ( c_sub )
        {
            if ( contact.name.is_empty() && ( 0 != ( contact.mask & CDM_PUBID ) ) )
            {
                c_sub->set_name( contact.pubid );
            } else
                c_sub->set_name( contact.name );

            if (c_sub->getkey().temp_type == TCT_UNKNOWN_MEMBER)
            {
                // unknown conference member has changed name
                if (ASSERT( cm ))
                    cm->update_name();
            } else
            {
                is_conference_member( c_sub->getkey() ); // update confas
            }
        }

        ASSERT( !c_confa );

        refilter = true;
    }

    if (0 != (contact.mask & CDM_STATUSMSG) && c_sub)
        c_sub->set_statusmsg( contact.statusmsg );

    if (0 != (contact.mask & CDM_DETAILS) && c_sub)
        c_sub->set_details(contact.details);

    if (0 != (contact.mask & CDM_STATE) && c_confa && c_confa->get_state() != contact.state)
    {
        if ( c_confa->get_state() == CS_ONLINE && contact.state == CS_OFFLINE )
        {
            c_confa->stop_av();
            c->proto_key = contact_id_s();

        } else if ( c_confa->get_state() == CS_OFFLINE && contact.state == CS_ONLINE )
        {
            DEFERRED_EXECUTION_BLOCK_BEGIN( 0 )
                gmsg<ISOGM_NOTICE>( (contact_root_c *)param, (contact_c *)param, NOTICE_CONFERENCE ).send();
            DEFERRED_EXECUTION_BLOCK_END( c_confa );
        }

        c_confa->set_state(contact.state);
    }

    if (0 != (contact.mask & CDM_STATE) && c_sub)
    {
        bool metaoffline = c_sub->get_state() != CS_UNKNOWN && c_sub->get_historian()->get_meta_state() == CS_OFFLINE;
        contact_state_e oldst = c_sub->get_state();
        c_sub->set_state( contact.state );

        if ( c_sub->get_state() != oldst )
        {
            if (!is_self)
            {
                if (contact_root_c *h = c_sub->get_historian())
                {
                    if (h->subcount() > 1)
                        g_app->update_protos_head();
                    contact_activity(h->getkey());
                }

                if ( oldst == CS_OFFLINE || oldst == CS_UNKNOWN )
                    if ( contact.state == CS_ONLINE )
                    {
                        play_sound( snd_friend_online, false );
                        contact_root_c *hst = c_sub->get_historian();

                        g_app->reset_undelivered_resend_cooldown( hst->getkey() );

                        if ( metaoffline && !hst->get_greeting().is_empty() && hst->get_greeting_allow_time() < ts::now()  )
                        {
                            DEFERRED_EXECUTION_BLOCK_BEGIN( 0 )

                                contact_c *c_sub = (contact_c *)param;
                                contact_root_c *hst = c_sub->get_historian();
                                time_t n = ts::now();
                                hst->set_greeting_last( n );
                                prf().dirtycontact( hst->getkey() );
                                ++n;
                                if ( hst->get_readtime() < n )
                                    hst->set_readtime( n );
                                gmsg<ISOGM_MESSAGE> msg( &contacts().get_self(), c_sub, MTA_UNDELIVERED_MESSAGE, 0 );

                                ts::str_c gs = c_sub->get_historian()->get_greeting();
                                gs.trim();

                                if ( !gs.is_empty() )
                                {
                                    emoti().parse( gs, true );
                                    gs.trim_right( CONSTASTR( "\r\n" ) );

                                    if ( !gs.is_empty() )
                                    {
                                        msg.post.message_utf8 = ts::refstring_t<char>::build( gs, g_app->global_allocator );
                                        msg.send();
                                    }
                                }

                            DEFERRED_EXECUTION_BLOCK_END(c_sub);
                        }
                    }

                if ( oldst == CS_ONLINE )
                    if (contact.state == CS_OFFLINE || contact.state == CS_UNKNOWN)
                    {
                        play_sound(snd_friend_offline, false);
                        if (c_sub->is_av())
                        {
                            c_sub->get_historian()->stop_av();
                            gmsg<ISOGM_NOTICE>(nullptr, nullptr, NOTICE_KILL_CALL_INPROGRESS).send();
                        }
                    }
            }

            if (contact.state == CS_ONLINE || contact.state == CS_OFFLINE)
            {
                if (oldst == CS_INVITE_RECEIVE)
                {
                    // accept ok

                    gmsg<ISOGM_MESSAGE> msg(c_sub, &get_self(), MTA_ACCEPT_OK, 0);
                    c_sub->getmeta()->fix_history(MTA_FRIEND_REQUEST, MTA_OLD_REQUEST);
                    msg.send();
                    serious_change = c_sub->getkey().protoid;
                    c_sub->reselect();


                }
                else if (oldst == CS_INVITE_SEND)
                {
                    // accepted
                    // avoid friend request
                    c_sub->getmeta()->fix_history(MTA_FRIEND_REQUEST, MTA_OLD_REQUEST);
                    gmsg<ISOGM_MESSAGE>(c_sub, &get_self(), MTA_ACCEPTED, 0).send();
                    serious_change = c_sub->getkey().protoid;
                    c_sub->reselect();
                }
            }

            if ( contact.state == CS_UNKNOWN )
            {
                if ( oldst == CS_OFFLINE || oldst == CS_ONLINE )
                    c_sub->set_statusmsg( ts::str_c() );
                else if ( oldst == CS_INVITE_SEND )
                {
                    c_sub->options().unmasked().set( contact_c::F_JUST_REJECTED );
                    gmsg<ISOGM_NOTICE>( c_sub->get_historian(), c_sub, NOTICE_FRIEND_REQUEST_SEND_OR_REJECT ).send();
                }
            } else
                c_sub->options().unmasked().clear( contact_c::F_JUST_REJECTED );

            if ( CS_UNKNOWN == oldst && c_sub->getmeta() == nullptr )
            {
                contact_root_c *meta = create_new_meta();
                meta->subadd(c_sub);
                serious_change = c_sub->getkey().protoid;
            }

            is_conference_member( c_sub->getkey() );
        }
    }
    if (0 != (contact.mask & CDM_GENDER) && c_sub)
    {
        c_sub->set_gender(contact.gender);
    }
    if (0 != (contact.mask & CDM_ONLINE_STATE) && c_sub)
    {
        c_sub->set_ostate(contact.ostate);
    }

    if (0 != (contact.mask & CDM_DATA) && c_sub)
    {
        if (c_sub->set_protodata(contact.pdata))
            prf().dirtycontact( c_sub->getkey() );
    }

    if (0 != (contact.mask & CDM_AVATAR_TAG) && !is_self && c_sub)
    {

        if (c_sub->avatar_tag() != contact.avatar_tag)
        {
            if (contact.avatar_tag == 0)
            {
                c_sub->set_avatar(nullptr, 0, 0);
                prf().set_avatar(c_sub->getkey(), ts::blob_c(), 0);
            }
            else if (active_protocol_c *ap = prf().ap(c_sub->getkey().protoid))
                ap->avatar_data_request( contact.key );

            c_sub->redraw();
            if (!is_self) contact_activity(c_sub->get_historian()->getkey());
        }
    }

    if (c_sub)
    {
        if (!c_sub->getkey().is_self && c_sub->getkey().temp_type == TCT_NONE)
            prf().dirtycontact(c_sub->getkey());
        if (c_sub->get_state() != CS_UNKNOWN || !contact.key.confmember )
            gmsg<ISOGM_V_UPDATE_CONTACT>(c_sub, refilter).send();
    }
    if (c_confa)
    {
        gmsg<ISOGM_V_UPDATE_CONTACT>(c_confa, refilter).send();
    }


    if (serious_change)
    {
        // serious change - new contact added. proto data must be saved
        if (active_protocol_c *ap = prf().ap(serious_change))
            ap->save_config(false);
    }

    return 0;
}

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_NEWVERSION>&nv)
{
    if (nv.ver.is_empty() || !nv.is_ok()) return 0; // notification about no new version
    bool cur64 = false;
    ts::str_c newver = cfg().autoupdate_newver( cur64 );
    if ( new_version( newver, nv.ver, nv.version64 && !cur64 ) )
        cfg().autoupdate_newver( nv.ver, nv.version64 );
    else if (nv.error_num != gmsg<ISOGM_NEWVERSION>::E_OK_FORCE)
        return 0;
    g_app->F_NEWVERSION( new_version() );
    self->redraw();
    if ( g_app->F_NEWVERSION() )
    {
        ts::str_c nvs( nv.ver );
        if ( nv.version64 ) nvs.append( CONSTASTR("/64") );
        gmsg<ISOGM_NOTICE>( self, nullptr, NOTICE_NEWVERSION, nvs ).send();
    }

    play_sound( snd_new_version, false );
    g_app->new_blink_reason( contact_key_s() ).new_version();

    return 0;
}

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_DELIVERED>&d)
{
    contact_key_s historian;
    if (prf().change_history_item(d.utag, historian))
    {
    do_deliver_stuff:
        contact_root_c *c = ts::ptr_cast<contact_root_c *>(find(historian));
        if (CHECK(c))
        {
            c->iterate_history([&](post_s &p)->bool {
                if ( p.utag == d.utag )
                {
                    ASSERT(p.type == MTA_UNDELIVERED_MESSAGE);
                    p.type = MTA_MESSAGE;
                    return true;
                }
                return false;
            });
        }
    } else
    {
        // may be no history saved? try find historian
        for(contact_c *c : arr)
            if (c->is_rootcontact())
                if (contact_root_c *h = ts::ptr_cast<contact_root_c *>(c))
                    if (h->find_post_by_utag(d.utag))
                    {
                        historian = c->getkey();
                        goto do_deliver_stuff;
                    }
    }

    return 0;
}

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_FILE>&ifl)
{
    switch (ifl.fctl)
    {
    case FIC_NONE:
        {
            if (ifl.data.size())
            {
                // incoming file data
                if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(ifl.i_utag))
                    ft->save( ifl.offset, ifl.data );
                return 0;
            }

            if (ifl.filename.get_length() == 0)
            {
                // query
                if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(ifl.i_utag))
                    ft->query(ifl.offset, (int)ifl.filesize);
                return 0;
            }

            contact_c *sender = find(ifl.sender);
            if (sender == nullptr) return 0; // sender. no sender - no message
            contact_root_c *historian = get_historian(sender, &get_self());

            if (auto *row = prf().get_table_unfinished_file_transfer().find<true>([&](const unfinished_file_transfer_s &uftr)->bool
                {
                    if (uftr.upload) return false;
                    if (ifl.sender != sender->getkey()) return false;
                    return ifl.filename.equals( uftr.filename ) && ifl.filesize == uftr.filesize;
                }))
            {
                // resume
                uint64 guiitm_utag = row->other.msgitem_utag;
                ts::wstr_c fod = row->other.filename_on_disk;

                row->deleted(); // this row not needed anymore
                prf().changed();

                if (guiitm_utag == 0 || !is_file_exists(fod) || nullptr != g_app->find_file_transfer_by_iutag(ifl.i_utag))
                    goto not_resume;

                auto &tft = prf().get_table_unfinished_file_transfer().getcreate(0);
                tft.other.utag = prf().getuid();
                tft.other.msgitem_utag = guiitm_utag;
                tft.other.filesize = ifl.filesize;
                tft.other.filename = ifl.filename;

                if (file_transfer_s *ft = g_app->register_file_transfer(historian->getkey(), ifl.sender, 0, ifl.filename, ifl.filesize))
                {
                    g_app->new_blink_reason(historian->getkey()).file_download_progress_add(ft->utag);

                    ft->filename_on_disk = fod;
                    tft.other.filename_on_disk = fod;
                    ft->i_utag = ifl.i_utag;
                    ft->resume();

                } else if (active_protocol_c *ap = prf().ap(ifl.sender.protoid))
                    ap->file_control(ifl.i_utag, FIC_REJECT);


            } else
            {
                not_resume:
                if (file_transfer_s *ft = g_app->register_file_transfer(historian->getkey(), ifl.sender, 0, ifl.filename, ifl.filesize))
                {
                    g_app->new_blink_reason(historian->getkey()).file_download_request_add(ft->utag);
                    ft->i_utag = ifl.i_utag;

                    if (ft->confirm_required())
                    {
                    confirm_req:
                        play_sound(snd_incoming_file, false);
                        gmsg<ISOGM_NOTICE> n( historian, sender, NOTICE_FILE, ft->text_for_notice() );
                        n.utag = ifl.i_utag;
                        n.send();
                    } else if (!ft->auto_confirm())
                        goto confirm_req;
                }
                else if (active_protocol_c *ap = prf().ap(ifl.sender.protoid))
                    ap->file_control(ifl.i_utag, FIC_REJECT);
            }

            historian->redraw();

        }
        break;
    case FIC_ACCEPT:
        if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(ifl.i_utag))
            ft->upload_accepted();
        break;
    case FIC_BREAK:
    case FIC_REJECT:
        DMSG("ftbreak " << ifl.i_utag);
        if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(ifl.i_utag))
            ft->kill(FIC_NONE);
        break;
    case FIC_PAUSE:
    case FIC_UNPAUSE:
        if (file_transfer_s *ft = g_app->find_file_transfer_by_iutag(ifl.i_utag))
            ft->pause_by_remote(FIC_PAUSE == ifl.fctl);
        break;
    case FIC_DISCONNECT:
    case FIC_UNKNOWN:
    case FIC_DONE:
        DMSG("fkill " << ifl.i_utag << (int)ifl.fctl );
        if ( file_transfer_s *ft = g_app->find_file_transfer_by_iutag( ifl.i_utag ) )
        {
            unfinished_file_transfer_s uft;
            ft->kill( ifl.fctl, &uft );
            file_transfer_s::upd_message_item(uft);
        }
        break;
    }

    return 0;
}

static void format_system_message( ts::str_c& sysmsg )
{
    ts::json_c json;
    json.parse( sysmsg );

    ts::str_c subj;
    ts::str_c text;
    ts::str_c desc;

    json.iterate( [&]( const ts::str_c &fname, const ts::json_c &v ) {

        if ( fname.equals( CONSTASTR( SMF_SUBJECT ) ) )
        {
            subj = v.as_string();
        }
        if ( fname.equals( CONSTASTR( SMF_TEXT ) ) )
        {
            text = v.as_string();
        }
        if ( fname.equals( CONSTASTR( SMF_DESCRIPTION ) ) )
        {
            desc = v.as_string();
        }
    } );

    sysmsg.clear();
    if ( !subj.is_empty() )
    {
        sysmsg.set( CONSTASTR("[b]") ).append(ts::to_utf8( TTT("Subject",454) ) ).append( CONSTASTR( "[/b]: " ) ).append( subj ).append_char( '\n' );

        if ( !text.is_empty() )
            sysmsg.append( ( CONSTASTR( "[b]" ) ) ).append( ts::to_utf8( TTT( "Message", 455 ) ) ).append( CONSTASTR( "[/b]: " ) ).append( text ).append_char( '\n' );

        if ( !desc.is_empty() )
            sysmsg.append( ( CONSTASTR( "[b]" ) ) ).append( ts::to_utf8( TTT("Description",271) ) ).append( CONSTASTR( "[/b]: " ) ).append( desc ).append_char( '\n' );

    } else
    {
        sysmsg.append( text );
    }

    sysmsg.trim();
}

void parse_links( ts::str_c &message, bool reset_n );

ts::uint32 contacts_c::gm_handler(gmsg<ISOGM_INCOMING_MESSAGE>&imsg)
{
    contact_c *sender = nullptr;
    contact_root_c *historian = nullptr;

    if ( imsg.gid.is_conference() && imsg.cid.is_contact() )
    {
        conference_s *c = prf().find_conference( imsg.gid, imsg.apid );
        if ( !c ) return 0;
        historian = c->confa;

        sender = find( contact_key_s(imsg.cid, imsg.apid) );
        if (!sender)
        {
            conference_member_s *cm = prf().find_conference_member( imsg.cid, imsg.apid );
            if ( !cm ) return 0;
            sender = cm->memba;
        }

    } else
    {
        sender = find(contact_key_s(imsg.cid, imsg.apid));
        if ( !sender ) return 0;
        historian = get_historian( sender, &get_self() );
    }

    if ( !sender || !historian )
        return 0;

    resort_list();
    switch (imsg.mt)
    {
    case MTA_CALL_STOP:
        if (historian->is_ringtone())
        {
            const post_s * p = historian->fix_history(MTA_INCOMING_CALL, MTA_INCOMING_CALL_CANCELED, sender->getkey(), historian->nowtime());
            if (p) gmsg<ISOGM_SUMMON_POST>(*p, true).send();
            historian->ringtone( sender, false );
        } else if (sender->is_av())
        {
            // hangup by remote peer

            ts::str_c times;
            if (av_contact_s *avc = g_app->avcontacts().find_inprogress( sender->avkey( historian->getkey() ) ))
            {
                int dt = static_cast<int>(ts::now() - avc->core->starttime);
                if (dt > 0)
                    times = to_utf8(text_seconds(dt));
            }

            const post_s * p = historian->fix_history(MTA_CALL_ACCEPTED, MTA_HANGUP, sender->getkey(), historian->nowtime(), times.is_empty() ? nullptr : &times);
            if (p) gmsg<ISOGM_SUMMON_POST>(*p, true).send();
            historian->av( sender, false, false );
        } else if (sender->is_calltone())
        {
            historian->calltone( sender, CALLTONE_HANGUP );
        }
        return 0;
    case MTA_CALL_ACCEPTED__:
        play_sound(snd_call_accept, false);
        if (!sender->is_calltone()) // ignore this message if no calltone
            return 0;
        imsg.mt = MTA_CALL_ACCEPTED; // replace type of message here
        break;
    case MTA_SYSTEM_MESSAGE:
        format_system_message( imsg.msgutf8 );
        if ( imsg.msgutf8.is_empty() )
            return 0;
        break;
    case MTA_FRIEND_REQUEST:
        if ( imsg.msgutf8.is_empty() )
            imsg.msgutf8 = ts::to_utf8( loc_text( loc_please_authorize ) );
        break;
    }

    ts::str_c m = imsg.msgutf8;
    m.trim();

    if (historian->getkey().is_conference())
    {
        ts::wstr_c my_name = contacts().calc_self_name( historian->getkey().protoid );
        ts::str_c message( m );
        text_adapt_user_input( message );
        parse_links( message, true );
        emoti().parse( message );

        if (historian->find_conference()->is_hl_message( from_utf8( message ), my_name ))
            imsg.mt = MTA_HIGHLIGHT_MESSAGE;
    }


    gmsg<ISOGM_MESSAGE> msg(sender, historian->getkey().is_conference() ? historian : &get_self(), imsg.mt, 0);
    msg.post.cr_time = imsg.create_time;
    msg.post.set_message_text( m );
    msg.send();

    contact_activity( historian->getkey() );
    switch (imsg.mt)
    {
    CASE_MESSAGE:
        g_app->new_blink_reason( historian->getkey() ).up_unread();
        historian->subactivity(sender->getkey());

        if (g_app->is_inactive( false, historian->getkey() ) )
        {
            bool show_desktop_notify = prf_options().is( UIOPT_SHOW_INCOMING_MSG_PNL );
            bool intrusive_behaviour = prf_options().is( UIOPT_INTRUSIVE_BEHAVIOUR );

            if ( int imnb = historian->get_imnb() )
            {
                if ( imnb & IMB_SUPPRESS )
                {
                    if ( (imnb & IMB_DESKTOP_NOTIFICATION) && imsg.mt != MTA_HIGHLIGHT_MESSAGE )
                        show_desktop_notify = false;

                    if ( (imnb & IMB_INTRUSIVE_BEHAVIOUR) && imsg.mt != MTA_HIGHLIGHT_MESSAGE)
                        intrusive_behaviour = false;

                } else
                {
                    if ( imnb & IMB_DESKTOP_NOTIFICATION )
                        show_desktop_notify = true;

                    if ( imnb & IMB_INTRUSIVE_BEHAVIOUR )
                        intrusive_behaviour = true;
                }
            }

            if ( show_desktop_notify )
                MAKE_ROOT<incoming_msg_panel_c>( historian, sender, msg.post );

            if ( intrusive_behaviour )
                g_app->bring2front( nullptr );
        }

        historian->execute_message_handler( m );
        // no break here!!!

    case MTA_FRIEND_REQUEST:
        if (MTA_FRIEND_REQUEST == imsg.mt)
            g_app->new_blink_reason( historian->getkey() ).friend_invite();

        if ( historian->get_imnb() == IMB_DONT_NOTIFY && imsg.mt != MTA_HIGHLIGHT_MESSAGE)
        {
            g_app->is_inactive( true, historian->getkey() );

        } else
        {
            if ( g_app->is_inactive( true, historian->getkey() ) || !msg.current )
                play_sound( snd_incoming_message, false );
            else if ( msg.current )
                play_sound( snd_incoming_message2, false );
        }

        break;
    case MTA_INCOMING_CALL:
        if (historian->get_aaac())
            DEFERRED_UNIQUE_CALL(0, DELEGATE(sender, b_accept_call), historian->get_aaac());
        else
            historian->ringtone( msg.sender );
        break;
    case MTA_CALL_ACCEPTED:
        historian->av( sender, true, historian->calltone(sender, CALLTONE_CALL_ACCEPTED));
        break;
    }

    return 0;
}

void contacts_c::unload()
{
    MEMT( MEMT_CONTACT_ITEM );

    prf().shutdown_aps();

    gmsg<ISOGM_SELECT_CONTACT>( nullptr, 0 ).send();

    for (contact_c *c : arr)
    {
        if (c->is_rootcontact())
            if ( gui_contact_item_c *gui_item = ts::ptr_cast<contact_root_c *>(c)->gui_item )
                TSDEL(gui_item);

        c->prepare4die(nullptr);
    }

    if (self)
        self = nullptr;

    arr.clear();
    self = TSNEW(contact_root_c, contact_key_s( true ));
    arr.add(self.get());
}

void contacts_c::contact_activity( const contact_key_s &ck )
{
    ASSERT(ck.is_meta() || ck.is_conference());

    ts::aint cnt = activity.count();
    if ( cnt && activity.last(contact_key_s()) == ck )
        return;

    for( int i=0;i<cnt; ++i )
    {
        if (activity.get(i) == ck)
        {
            activity.remove_slow(i);
            break;
        }
    }

    activity.add(ck);
    resort_list();
}

void contacts_c::toggle_tag( ts::aint i)
{
    enabled_tags.set_bit(i, !enabled_tags.get_bit(i));
    if (!enabled_tags.is_any_bit())
        enabled_tags.clear();

    ts::str_c tags;
    ts::aint cnt = all_tags.size();
    for ( ts::aint j = 0; j < cnt; ++j)
        if (enabled_tags.get_bit(j))
            tags.append(all_tags.get(j)).append_char(',');
    tags.trunc_length();
    prf().tags( tags );
}

void contacts_c::replace_tags(int i, const ts::str_c &ntn)
{
    iterate_root_contacts([&](contact_root_c *r)->bool
    {
        ts::aint ti = r->get_tags().find( all_tags.get(i) );
        if (ti >= 0)
        {
            ts::astrings_c tt = r->get_tags();
            tt.remove_fast( ti );
            tt.add(ntn);
            tt.kill_dups_and_sort(true);
            r->set_tags(tt);
            prf().dirtycontact( r->getkey() );
        }

        return true;
    });

    if (enabled_tags.get_bit(i))
    {
        ts::astrings_c etags( prf().tags(), ',' );
        etags.find_remove_fast( all_tags.get(i) );
        etags.add(ntn);
        etags.kill_dups_and_sort(true);
        prf().tags(etags.join(','));
    }

    rebuild_tags_bits( false );
}

void contacts_c::rebuild_tags_bits(bool refresh_ui)
{
    all_tags.clear();
    ts::tmp_pointers_t<contact_root_c, 0> roots;
    iterate_root_contacts([&](contact_root_c *r)->bool
    {
        roots.add(r);
        all_tags.add( r->get_tags() );
        return true;
    });
    all_tags.kill_dups_and_sort();
    for(contact_root_c *r : roots)
        r->rebuild_tags_bits(all_tags);

    enabled_tags.clear();

    for( ts::token<char> t( prf().tags(), ',' ); t; ++t )
    {
        ts::aint bi = all_tags.find(*t);
        if (bi >= 0) enabled_tags.set_bit(bi, true);
    }

    if (refresh_ui && prf().is_loaded() && prf_options().is(UIOPT_TAGFILETR_BAR))
    {
        g_app->recreate_ctls(true, false);
    }
}

void contacts_c::cleanup()
{
    if ( arr.size() == 0 )
        return;

    if ( cleanup_index >= arr.size() )
        cleanup_index = 0;

    int start_check_index = cleanup_index;
    int checked_count = 0;

    for(; checked_count < 5;)
    {
        contact_c *c = arr.get( cleanup_index );
        if ( c->is_rootcontact() )
        {
            if ( ts::ptr_cast<contact_root_c *>( c )->is_ancient_history() )
            {
                prf().unload_history( c->getkey() );
                ts::ptr_cast<contact_root_c *>( c )->unload_history();
            }
            ++checked_count;
        }

        ++cleanup_index;

        if ( cleanup_index >= arr.size() )
            cleanup_index = 0;
        if ( cleanup_index == start_check_index )
            break;
    }

}




void contact_root_c::toggle_tag(const ts::asptr &t)
{
    ts::aint i = get_tags().find(t);
    if ( i>=0 )
        tags.remove_slow(i);
    else
    {
        tags.add(t);
        tags.sort(true);
    }
    prf().dirtycontact( getkey() );
    contacts().rebuild_tags_bits();
}

void contact_root_c::rebuild_tags_bits(const ts::astrings_c &allhts)
{
    tags_bits.clear();
    for( const ts::str_c&ht : tags )
    {
        ts::aint i = allhts.find(ht);
        if (i >= 0)
            tags_bits.set_bit(i, true);
    }
}


contact_c * contact_root_c::subgetadd(const contact_key_s&k)
{
    for (contact_c *c : subcontacts)
        if (c->getkey() == k) return c;
    contact_c *c = TSNEW(contact_c, k);
    subcontacts.add(c);
    c->setmeta(this);
    return c;
}
contact_c * contact_root_c::subget_proto(int protoid)
{
    for (contact_c *c : subcontacts)
        if (c->getkey().protoid == (unsigned)protoid) return c;
    return nullptr;
}

void contact_root_c::subaddconference(contact_c *c)
{
    if (ASSERT(key.is_conference() && !subpresent(c->getkey()) && c->getkey().protoid == getkey().protoid))
    {
        subcontacts.add(c);
    }
}


void contact_root_c::subadd(contact_c *c)
{
    if (ASSERT(key.is_meta() && !subpresent(c->getkey())))
    {
        subcontacts.add(c);
        c->setmeta(this);
    }
}

bool contact_root_c::subdel(contact_c *c)
{
    if (ASSERT(is_meta() && subpresent(c->getkey())))
    {
        c->prepare4die(this);
        subcontacts.find_remove_slow(c);
    }
    return subcontacts.size() == 0;
}
bool contact_root_c::subremove( contact_c *c )
{
    if ( ASSERT( is_meta() && subpresent( c->getkey() ) ) )
        subcontacts.find_remove_slow( c );
    return subcontacts.size() == 0;
}
void contact_root_c::subdelall()
{
    for (contact_c *c : subcontacts)
        c->prepare4die(this);
    subcontacts.clear();
}

contact_c * contact_root_c::subget_only_marked_defaul() const
{
    for (contact_c *c : subcontacts)
        if (c->options().is(contact_c::F_DEFALUT))
            return c;
    return nullptr;
}

contact_c * contact_root_c::subget_smart(why_this_subget_e &why) const
{
    if (getkey().is_conference())
    {
        why = WTS_ONLY_ONE;
        return (contact_c *)this;
    }

    if (subcontacts.size() == 0) return nullptr;

    if (subcontacts.size() == 1)
    {
        why = WTS_ONLY_ONE;
        return subcontacts.get(0);
    }

    for (contact_c *c : subcontacts)
        if (c->options().is(contact_c::F_DEFALUT) && c->get_state() == CS_ONLINE)
        {
            why = WTS_MARKED_DEFAULT;
            return c;
        }

    for (contact_c *c : subcontacts)
        if (c->options().unmasked().is(contact_c::F_LAST_ACTIVITY) && c->get_state() == CS_ONLINE)
        {
            why = WTS_BY_LAST_ACTIVITY;
            return c;
        }

    contact_c *target_contact = nullptr;
    int prior = 0;
    contact_state_e st = contact_state_check;
    bool real_offline_messaging = false;
    bool is_default = false;

    for (contact_c *c : subcontacts)
    {
        if (active_protocol_c *ap = prf().ap(c->getkey().protoid))
        {
            auto is_better = [&]()->bool {

                if (nullptr == target_contact)
                    return true;

                if (CS_ONLINE != st && CS_ONLINE == c->get_state())
                    return true;

                if (CS_ONLINE != st && CS_ONLINE != c->get_state())
                {
                    bool rom = 0 != (ap->get_features() & PF_OFFLINE_MESSAGING);

                    if (rom && !real_offline_messaging)
                        return true;

                    if (c->options().is(contact_c::F_DEFALUT) && !is_default)
                        return true;

                    if (ap->get_priority() > prior)
                        return true;
                }

                if (CS_ONLINE == st && CS_ONLINE == c->get_state())
                {
                    ASSERT(!c->options().is(contact_c::F_DEFALUT)); // because default + online the best choice

                    if (ap->get_priority() > prior)
                        return true;
                }

                return false;
            };

            if (is_better())
            {
                target_contact = c;
                prior = ap->get_priority();
                st = c->get_state();
                real_offline_messaging = 0 != (ap->get_features() & PF_OFFLINE_MESSAGING);
                is_default = c->options().is(contact_c::F_DEFALUT);
            }
        }
    }

    ASSERT(target_contact);

    why = is_default ? WTS_MARKED_DEFAULT : WTS_ONLY_ONE;
    return target_contact;
}

ts::str_c contact_root_c::compile_pubid() const
{
    ASSERT(is_meta());
    ts::str_c x;
    for (contact_c *c : subcontacts)
        x.append('~', c->get_pubid_desc());
    return x;
}
ts::str_c contact_root_c::compile_name() const
{
    ASSERT(is_meta());
    why_this_subget_e why;
    contact_c *defc = subget_smart(why);
    if (defc == nullptr) return ts::str_c();
    ts::str_c x(defc->get_name(false));
    for (contact_c *c : subcontacts)
        if (x.find_pos(c->get_name(false)) < 0)
            x.append_char('~').append(c->get_name(false));
    return x;
}

ts::str_c contact_root_c::compile_statusmsg() const
{
    ASSERT(is_meta());
    why_this_subget_e why;
    contact_c *defc = subget_smart(why);
    if (defc == nullptr) return ts::str_c();
    ts::str_c x(defc->get_statusmsg(false));
    for (contact_c *c : subcontacts)
        if (x.find_pos(c->get_statusmsg(false)) < 0)
            x.append_char('~').append(c->get_statusmsg(false));
    return x;
}

contact_state_e contact_root_c::get_meta_state() const
{
    if (key.is_conference())
        return get_state();

    if (subcontacts.size() == 0) return CS_ROTTEN;
    contact_state_e rst = contact_state_check;
    for (contact_c *c : subcontacts)
        if (c->get_state() == CS_REJECTED) return CS_REJECTED;
        else if (c->get_state() == CS_INVITE_SEND) return CS_INVITE_SEND;
        else if (c->get_state() == CS_INVITE_RECEIVE) return CS_INVITE_RECEIVE;
        else if (c->get_state() != rst)
        {
            if (rst != contact_state_check)
                return contact_state_check;
            rst = c->get_state();
        }
        return rst;
}

contact_online_state_e contact_root_c::get_meta_ostate() const
{
    if (subcontacts.size() == 0) return COS_ONLINE;
    contact_online_state_e s = COS_ONLINE;
    for (contact_c *c : subcontacts)
        if (c->get_ostate() > s) s = c->get_ostate();
    return s;
}

contact_gender_e contact_root_c::get_meta_gender() const
{
    if (subcontacts.size() == 0) return CSEX_UNKNOWN;
    contact_gender_e rgender = CSEX_UNKNOWN;
    for (contact_c *c : subcontacts)
        if (c->get_gender() != rgender)
        {
            if (rgender != CSEX_UNKNOWN) return CSEX_UNKNOWN;
            rgender = c->get_gender();
        }
    return rgender;
}

void contact_root_c::subactivity(const contact_key_s &ck)
{
    why_this_subget_e why;
    contact_c *old = subget_smart(why);
    for (contact_c *c : subcontacts)
        c->options().unmasked().init(contact_c::F_LAST_ACTIVITY, c->getkey() == ck);

    if (old != subget_smart(why))
        g_app->update_protos_head();
}

bool contact_root_c::b_load( RID, GUIPARAM n )
{
    int n_load = as_int( n );
    load_history( n_load );
    reselect( RSEL_INSERT_NEW );
    return true;
}

void contact_root_c::del_history()
{
    history_touch();
    unload_history();
    prf().kill_history(getkey());
}

void contact_root_c::del_history(uint64 utag)
{
    history_touch();

    ts::aint cnt = history.size();
    for (int i = 0; i < cnt; ++i)
    {
        post_s *p = history.get( i );
        if (p->utag == utag)
        {
            history.remove_slow(i);
            posts.dealloc(p);
            break;
        }
    }
    prf().kill_history_item(utag);
}


#if 0
void contact_root_c::make_time_unique(time_t &t) const
{
    if (history.size() == 0 || t < history.get(0)->recv_time)
        prf().load_history(getkey()); // load whole history to correct uniquzate t

    for (const post_s *p : history)
        if (p->recv_time == t)
            ++t;
        else if (p->recv_time > t)
            break;
}
#endif // 0

int contact_root_c::calc_unread() const
{
    if (keep_history())
        return prf().calc_history_after(getkey(), get_readtime(), true);

    time_t rt = get_readtime();
    int cnt = 0;
    for ( ts::aint i = history.size() - 1; i >= 0; --i)
    {
        const post_s *p = history.get(i);
        if (p->recv_time <= rt) break;
        if (p->mt() == MTA_MESSAGE)
            ++cnt;
    }
    return cnt;
}

bool contact_root_c::is_active() const
{
    if (g_app->F_SPLIT_UI())
    {
        rectengine_root_c *root = nullptr;

        if ( RID hconv = HOLD( g_app->main ).as<mainrect_c>().find_conv_rid( getkey() ) )
            root = HOLD( hconv )( ).getroot();
        else
            return false;
        if ( !CHECK( root ) ) return false;

        if ( root->getrect().getprops().is_collapsed() )
            return false;
        if ( !root->is_foreground() )
            return false;

        return true;
    }

    return gui_item && gui_item->getprops().is_active();
}

void contact_root_c::export_history(const ts::wsptr &templatename, const ts::wsptr &fname)
{
    history_touch();

    if (is_meta() || getkey().is_conference())
    {
        struct loader
        {
            contact_root_c *me;
            ts::shared_ptr<crallocator> a = TSNEW( crallocator );
            ts::struct_buf_t< post_s, 128 > posts;
            ts::pointers_t< post_s, 128 > hist;
            ts::uint8 mt;

            ~loader()
            {
                for ( post_s *p : hist )
                    posts.dealloc( p );
            }

            static post_s *alcpost( const ts::asptr&t, void *prm )
            {
                loader *ldr = (loader *)prm;

                post_s *p = ldr->posts.alloc();
                ldr->hist.add(p);
                p->message_utf8 = ts::refstring_t<char>::build( t, ldr->a );
                p->type = ldr->mt;
                return p;
            }
        } l;

        l.me = this;
        l.mt = getkey().is_conference() ? message_type_app_values : 0;

        prf().load_history( getkey(), loader::alcpost, &l );

        ts::tmp_buf_c buf; buf.load_from_file(templatename);
        ts::pstr_c tmpls(buf.cstr());

        ts::str_c time, tname, text, linebreak(CONSTASTR("\r\n")), link;

        static ts::asptr bb_tags[] = { CONSTASTR("u"), CONSTASTR("i"), CONSTASTR("b"), CONSTASTR("s") };
        struct bbcode_s
        {
            ts::str_c bb0;
            ts::str_c be0;

            ts::str_c bb;
            ts::str_c be;
        } bbr[ARRAY_SIZE(bb_tags)];

        int bi = 0;
        for (ts::asptr &b : bb_tags)
        {
            bbr[bi].bb0.set(CONSTASTR("[")).append(b).append(CONSTASTR("]"));
            bbr[bi].bb = bbr[bi].bb0;
            bbr[bi].be0.set(CONSTASTR("[/")).append(b).append(CONSTASTR("]"));
            bbr[bi].be = bbr[bi].be0;
            ++bi;
        }

        auto bbrepls = [&](ts::str_c &s)
        {
            for (bbcode_s &b : bbr)
            {
                s.replace_all(b.bb0, b.bb);
                s.replace_all(b.be0, b.be);
            }
        };

        auto do_repls = [&](ts::str_c &s)
        {
            s.replace_all(CONSTASTR("{TIME}"), time);
            s.replace_all(CONSTASTR("{NAME}"), tname);
            s.replace_all(CONSTASTR("{TEXT}"), text);
        };

        auto store = [&](const ts::str_c &s)
        {
            ts::str_c ss(s);
            do_repls(ss);
            buf.append_buf(ss.cstr(), ss.get_length());
        };

        auto find_indx = [&](const ts::asptr &section, bool repls)->ts::str_c
        {
            int index = tmpls.find_pos(section);
            if (index >= 0)
            {
                index += section.l;
                int index_end = tmpls.find_pos(index, CONSTASTR("===="));
                if (index_end < 0) index_end = tmpls.get_length();

                ts::str_c s(tmpls.substr(index, index_end));
                if (repls) do_repls(s);
                return s;
            }
            return ts::str_c();
        };

        tname = get_name();

        ts::localtime_s tt;
        ts::swstr_t<-128> tstr;
        tstr.append_as_uint(tt.tm_hour);
        if (tt.tm_min < 10)
            tstr.append(CONSTWSTR(":0"));
        else
            tstr.append_char(':');
        tstr.append_as_uint(tt.tm_min);
        time = ts::to_utf8(tstr);

        ts::str_c hdr = find_indx(CONSTASTR("====header"), true);
        ts::str_c footer = find_indx(CONSTASTR("====footer"), true);
        ts::str_c mine = find_indx(CONSTASTR("====mine"), false);
        ts::str_c namedmine = find_indx(CONSTASTR("====namedmine"), false);
        ts::str_c other = find_indx(CONSTASTR("====other"), false);
        ts::str_c namedother = find_indx(CONSTASTR("====namedother"), false);
        ts::str_c datesep = find_indx(CONSTASTR("====dateseparator"), false);
        ts::abp_c sets; sets.load(find_indx(CONSTASTR("====replace"), false));
        if (ts::abp_c * lbr = sets.get(CONSTASTR("linebreak")))
            linebreak = lbr->as_string();
        if (ts::abp_c * lbr = sets.get(CONSTASTR("link")))
            link = lbr->as_string();

        bi = 0;
        for (ts::asptr &b : bb_tags)
        {
            if (ts::abp_c * bb = sets.get(ts::str_c(CONSTASTR("bb-"),b)))
                bbr[bi].bb = bb->as_string();
            if (ts::abp_c * bb = sets.get(ts::str_c(CONSTASTR("be-"),b)))
                bbr[bi].be = bb->as_string();
            ++bi;
        }

        buf.clear();

        if (!hdr.is_empty())
            buf.append_buf(hdr.cstr(), hdr.get_length());


        tm last_post_time = {};
        contact_c *prev_sender = nullptr;
        ts::aint cnt = l.hist.size();
        for (int i = 0; i < cnt; ++i)
        {
            const post_s *post = l.hist.get(i);

            ts::localtime_s tmtm(post->recv_time);

            if (!datesep.is_empty())
            {
                if (tmtm.tm_year != last_post_time.tm_year || tmtm.tm_mon != last_post_time.tm_mon || tmtm.tm_mday != last_post_time.tm_mday)
                {
                    text_set_date(tstr, from_utf8(prf().date_sep_template()), tmtm);
                    time = ts::to_utf8(tstr);
                    store(datesep);
                    prev_sender = nullptr;
                }
            }
            last_post_time = tmtm;

            tstr.clear();
            tstr.append_as_uint(last_post_time.tm_hour);
            if (last_post_time.tm_min < 10)
                tstr.append(CONSTWSTR(":0"));
            else
                tstr.append_char(':');
            tstr.append_as_uint(last_post_time.tm_min);

            //text_set_date(tstr, from_utf8(prf().date_msg_template()), last_post_time);
            time = ts::to_utf8(tstr); //-V519
            bool is_mine = post->sender.is_self;
            contact_c *sender = contacts().find(post->sender);
            if (prev_sender != sender)
            {
                if (sender)
                {
                    contact_c *cs = sender;
                    if (cs->getkey().is_self && post->receiver.protoid)
                        cs = contacts().find_subself(post->receiver.protoid);

                    tname = cs->get_name();
                    bbrepls(tname);

                }
                else
                    tname = CONSTASTR("?");
            }

            text = post->message_utf8->cstr();
            text.replace_all(CONSTASTR("\n"), linebreak);
            if (!link.is_empty())
            {
                ts::ivec2 linkinds;
                for (int j = 0; text_find_link(text.as_sptr(), j, linkinds);)
                {
                    ts::str_c lnk = link;
                    lnk.replace_all(CONSTASTR("{LINK}"), text.substr(linkinds.r0, linkinds.r1));
                    text.replace(linkinds.r0, linkinds.r1 - linkinds.r0, lnk);
                    j = linkinds.r0 + lnk.get_length();
                }
            }
            bbrepls(text);

            if (prev_sender != sender)
                store(is_mine ? namedmine : namedother);
            else
                store(is_mine ? mine : other);
            prev_sender = sender;
        }


        if (!footer.is_empty())
            buf.append_buf(footer.cstr(), footer.get_length());
        buf.save_to_file(fname);
    }
}


void contact_root_c::load_history( ts::aint n_last_items )
{
    MEMT( MEMT_PROFILE_HISTORY );

    history_touch();

    ASSERT(get_historian() == this);

    if ( getkey().is_conference() && pubid.is_empty() )
        return;

    if ( n_last_items < 0 )
    {
        if ( !options().unmasked().is( F_HISTORY_NEED_LOAD ) )
            return;

        // WHOLE! WHOLE HISTORY!!!
        prf().flush_history_now();
        prf().unload_history( getkey() );
        unload_history();

        // do not use profile history table view to for whole history
        struct loader
        {
            contact_root_c *me;
            ts::shared_ptr<crallocator> a = TSNEW( crallocator );
            ts::uint8 mt = 0;
            static post_s *alcpost( const ts::asptr&t, void *prm )
            {
                loader *ldr = (loader *)prm;
                post_s *p = ldr->me->add_history_unsafe();
                p->message_utf8 = ts::refstring_t<char>::build( t, ldr->a );
                p->type = ldr->mt;
                return p;
            }
        } l;

        l.me = this;
        l.mt = getkey().is_conference() ? message_type_app_values : 0;

        prf().load_history( getkey(), loader::alcpost, &l );
        options().unmasked().clear( F_HISTORY_NEED_LOAD );
        return;
    }
    options().unmasked().clear( F_HISTORY_NEED_LOAD );
    time_t before = 0;
    if (history.size()) before = history.get(0)->recv_time;
    ts::tmp_tbuf_t<int> ids;
    prf().load_history(getkey(), before, n_last_items, ids);

    for (int id : ids)
    {
        auto * row = prf().get_table_history().find<true>(id);
        if (ASSERT(row && row->other.historian == getkey()))
        {
            if (MTA_RECV_FILE == row->other.mt() || MTA_SEND_FILE == row->other.mt())
            {
                if (nullptr == prf().get_table_unfinished_file_transfer().find<true>([&](const unfinished_file_transfer_s &uftr)->bool { return uftr.msgitem_utag == row->other.utag; }))
                {
                    if (row->other.message_utf8->cstr().s[ 0 ] == '?')
                    {
                        row->other.message_utf8->setchar( 0, '*' );
                        row->changed();
                        prf().changed();
                    }
                }
                else
                {
                    if (row->other.message_utf8->cstr().s[ 0 ] != '?')
                    {
                        if (row->other.message_utf8->cstr().s[ 0 ] == '*')
                            row->other.message_utf8->setchar(0, '?');
                        else
                        {
                            ts::str_c m( CONSTASTR("?"), row->other.message_utf8->cstr() );
                            row->other.set_message_text(m);
                        }
                        row->changed();
                        prf().changed();
                    }
                }
            }

            add_history( row->other.recv_time, row->other.cr_time ) = row->other;
        }
    }
}

const avatar_s *contact_root_c::get_avatar() const
{
    MEMT( MEMT_AVATARS );

    if (getkey().is_conference())
        return super::get_avatar();

    const avatar_s * r = nullptr;
    for (const contact_c *c : subcontacts)
    {
        if (c->get_options().is(contact_c::F_AVA_DEFAULT) && c->get_avatar())
            return c->get_avatar();
        if (c->get_avatar() && (c->get_options().is(contact_c::F_DEFALUT) || r == nullptr))
            r = c->get_avatar();
    }
    return r;
}

namespace
{
    struct mhrun_s
    {
        ts::wstr_c cmd;
        ts::wstr_c param;
        ts::wstr_c tmpp;
        ts::str_c message;
        int tag;
        msg_handler_e mht;

        static bool is_unreserved( char c )
        {
            if ( c >= 'a' && c <= 'z' ) return true;
            if ( c >= 'A' && c <= 'Z' ) return true;
            if ( c >= '0' && c <= '9' ) return true;
            return c == '-' || c == '_' || c == '.' || c == '~';
        }

        void yo()
        {
            ts::wstr_c fn;
            if ( MH_AS_PARAM == mht )
            {
                // percent encode message

                ts::wstr_c msgencoded;
                int cnt = message.get_length();
                for(int i=0;i<cnt;++i)
                {
                    char c = message.get_char( i );
                    if ( is_unreserved( c ) )
                        msgencoded.append_char( c );
                    else
                        msgencoded.append_char( '%' ).append_as_hex( (ts::uint8)c );
                }

                param.replace_all( CONSTWSTR("<param>"), msgencoded );
            } else if ( MH_VIA_FILE == mht )
            {
                fn = ts::fn_join( tmpp, ts::wmake( ts::now() ).append_char('-').append_as_uint(tag).append(CONSTWSTR(".txt")) );
                ts::buf_c b;
                b.append_buf( message.cstr(), message.get_length() );
                b.save_to_file( fn );
                if ( fn.find_pos( ' ' ) >= 0 )
                    fn.insert( 0, '\"' ).append_char( '\"' );

                param.replace_all( CONSTWSTR( "<param>" ), fn );
            }

            ts::process_handle_s ph;
            ts::master().start_app( cmd, param, &ph, false );
            if (ts::master().wait_process( ph, 10000 ))
            {
                if (!fn.is_empty())
                    ts::kill_file(fn);
            }


            TSDEL( this );
        }
    };
}

void contact_root_c::execute_message_handler( const ts::asptr &utf8msg )
{
    if ( mht == MH_NOT ) return;
    if ( mhc.is_empty() ) return;

    mhrun_s *mhr = TSNEW( mhrun_s );
    mhr->mht = mht;
    mhr->message.set( utf8msg );

    {

        ts::wstrings_c s; s.qsplit( mhc );
        mhr->cmd = s.get( 0 );
        s.remove_slow( 0 );
        mhr->param = s.join( ' ' );
    } // str ref should be decremented now

    mhr->tmpp = cfg().temp_folder_handlemsg();
    path_expand_env( mhr->tmpp, contactidfolder() );
    ts::make_path( mhr->tmpp, 0 );

    mhr->tag = gui->get_free_tag();

    ts::master().sys_start_thread( DELEGATE( mhr, yo ) );
}

bool contact_root_c::keep_history() const
{
    if (g_app->F_READONLY_MODE() && !getkey().is_self) return false;
    if (key.is_conference() && pubid.is_empty())
        return false;
    if (KCH_ALWAYS_KEEP == keeph) return true;
    if (KCH_NEVER_KEEP == keeph) return false;
    return prf_options().is(MSGOP_KEEP_HISTORY);
}

ts::wstr_c contact_root_c::contactidfolder() const
{
    return ts::wmake<uint>(getkey().contactid);
}

void contact_root_c::send_file(ts::wstr_c fn)
{
    why_this_subget_e why;
    contact_c *cdef = subget_smart(why);
    contact_c *c_file_to = nullptr;
    subiterate([&](contact_c *c) {
        active_protocol_c *ap = prf().ap(c->getkey().protoid);
        if (ap && 0 != (PF_SEND_FILE & ap->get_features()))
        {
            if (c_file_to == nullptr || (cdef == c && c->get_state() == CS_ONLINE))
                c_file_to = c;
            if (c->get_state() == CS_ONLINE && c_file_to != c && c_file_to->get_state() != CS_ONLINE)
                c_file_to = c;
        }
    });

    if (c_file_to)
    {
        g_app->register_file_transfer(getkey(), c_file_to->getkey(), 0, fn, 0 /* 0 means send */);
    }
}

void contact_root_c::stop_av()
{
    if ( getkey().is_conference() )
    {
        av( nullptr, false, false );
        return;
    }

    ringtone(nullptr, false);
    av(nullptr, false, false);
    calltone(nullptr, CALLTONE_HANGUP);
}

bool contact_root_c::ringtone( contact_c *sub, bool activate, bool play_stop_snd )
{
    if (!activate && get_aaac())
    {
        ASSERT(!is_ringtone());
        return false;
    }

    auto subringtone = [&]( contact_c *c ) -> bool
    {
        if ( CS_ROTTEN != c->get_state() )
        {
            bool wasrt = c->get_options().unmasked().is( F_RINGTONE );
            c->options().unmasked().init( F_RINGTONE, activate );
            if ( wasrt && !activate )
            {
                gmsg<ISOGM_CALL_STOPED>(c).send();
                return true;
            }
        }
        return false;
    };

    bool was_stoped = false;
    if ( sub )
        was_stoped = subringtone( sub );
    else for ( contact_c *c : subcontacts )
        was_stoped |= subringtone( c );

    opts.unmasked().clear(F_RINGTONE);
    for (contact_c *c : subcontacts)
        if (c->is_ringtone())
        {
            opts.unmasked().set(F_RINGTONE);
            break;
        }

    g_app->new_blink_reason(getkey()).ringtone(activate);
    g_app->update_ringtone(this, sub, play_stop_snd);

    return was_stoped;
}

void contact_root_c::av( contact_c *sub, bool f, bool camera_ )
{
    if (getkey().is_conference())
    {
        ASSERT( getkey().temp_type == TCT_CONFERENCE );
        opts.unmasked().init( F_AV_INPROGRESS, f );

        if ( f )
        {
            conference_s *confa = prf().find_conference_by_id( getkey().contactid );
            bool a = confa->flags.is( conference_s::F_MIC_ENABLED );
            bool b = confa->flags.is( conference_s::F_SPEAKER_ENABLED );

            int tag = gui->get_free_tag();
            g_app->avcontacts().set_tag( this, tag );
            for ( contact_c *c : subcontacts )
                if ( av_contact_s *avc = g_app->update_av( this, c, true ) )
                    avc->set_so_audio( false, a, b );
            g_app->avcontacts().del(tag);
        } else
            g_app->update_av( this, nullptr, false );

        return;
    }

    auto subrav = [&]( contact_c *c )
    {
        bool wasav = c->get_options().unmasked().is( F_AV_INPROGRESS );
        c->options().unmasked().init( F_AV_INPROGRESS, f );
        if ( wasav && !f )
            gmsg<ISOGM_CALL_STOPED>( c ).send();
    };

    if ( sub )
        subrav( sub );
    else for ( contact_c *c : subcontacts )
        subrav( c );

    bool wasav = opts.unmasked().is(F_AV_INPROGRESS);
    opts.unmasked().init(F_AV_INPROGRESS, false);
    for (contact_c *c : subcontacts) if (c->is_av())
    {
        opts.unmasked().set(F_AV_INPROGRESS);
        break;
    }

    g_app->update_av( this, sub, opts.unmasked().is( F_AV_INPROGRESS ), camera_ );

    if (wasav && opts.unmasked().is(F_AV_INPROGRESS) != wasav)
        play_sound(snd_hangup, false);
}

bool contact_root_c::calltone( contact_c *sub, calltone_e ctt )
{
    bool f = ctt == CALLTONE_VOICE_CALL || ctt == CALLTONE_VIDEO_CALL;
    bool vc = false;

    if (sub)
    {
        vc = sub->options().unmasked().is(F_VIDEOCALL);
        sub->options().unmasked().init(F_CALLTONE, f);
        sub->options().unmasked().init(F_VIDEOCALL, ctt == CALLTONE_VIDEO_CALL);
    } 
    else for (contact_c *c : subcontacts)
        c->options().unmasked().init(F_CALLTONE, ctt == CALLTONE_VOICE_CALL || ctt == CALLTONE_VIDEO_CALL),
        c->options().unmasked().init(F_VIDEOCALL, ctt == CALLTONE_VIDEO_CALL);

    bool wasct = opts.unmasked().is(F_CALLTONE);
    opts.unmasked().clear(F_CALLTONE);
    for (contact_c *c : subcontacts)
        if (c->is_calltone())
        {
            opts.unmasked().set(F_CALLTONE);
            break;
        }

    if (opts.unmasked().is(F_CALLTONE) != wasct)
    {
        if (wasct)
            stop_sound(snd_calltone);
        else
            play_sound(snd_calltone, true);

        if (wasct && !f)
        {
            if (CALLTONE_CALL_ACCEPTED == ctt)
                return vc;

            if ( sub )
                gmsg<ISOGM_CALL_STOPED>( sub ).send();
            else for ( contact_c *c : subcontacts )
                gmsg<ISOGM_CALL_STOPED>( c ).send();

            return true;
        }
    }
    return false;
}

const post_s * contact_root_c::fix_history(message_type_app_e oldt, message_type_app_e newt, const contact_key_s& sender, time_t update_time, const ts::str_c *update_text)
{
    const post_s * r = nullptr;
    ts::tmp_tbuf_t<uint64> updated;
    ts::aint ri = iterate_history_changetime([&](post_s &p)
    {
        if (oldt == p.type && (sender.is_empty() || p.sender == sender))
        {
            p.type = newt;
            if (update_text) p.set_message_text( *update_text );
            updated.add(p.utag);
            if (update_time)
            {
                p.recv_time = update_time++;
                p.cr_time = p.recv_time;
            }
        }
        return true;
    });
    if (ri >= 0) r = history.get(ri);
    for (uint64 utag : updated)
    {
        const post_s *p = find_post_by_utag(utag);
        if (ASSERT(p))
            prf().change_history_item(getkey(), *p, HITM_MT | HITM_TIME | (update_text ? HITM_MESSAGE : 0));
    }
    return r;
}

void contact_root_c::change_history_items( const contact_key_s &old_sender, const contact_key_s &new_sender )
{
    for( post_s *p : history )
    {
        if ( p->sender == old_sender )
            p->sender = new_sender;
    }
    prf().change_history_items( getkey(), old_sender, new_sender );
}

void contact_root_c::add_message( const ts::str_c& utf8msg )
{
    gmsg<ISOGM_MESSAGE> msg( this, this, MTA_MESSAGE, 0 );

    msg.post.recv_time = ts::now();
    msg.post.cr_time = msg.post.recv_time;
    msg.post.set_message_text( utf8msg );
    msg.info = true;
    msg.send();

}

void contact_root_c::setup(const contacts_s * c, time_t nowtime)
{
    super::setup( c, nowtime );

    set_comment(c->comment);
    greeting = c->greeting;
    set_tags(c->tags);
    set_readtime(ts::tmin(nowtime, c->readtime));
    set_mhcmd( ts::from_utf8(c->msghandler) );

    keeph = (keep_contact_history_e)((c->options >> 16) & 7);
    aaac = (auto_accept_audio_call_e)((c->options >> 19) & 3);
    mht = (msg_handler_e)( ( c->options >> 21 ) & 7 );
    imnb = ( c->options >> 24 ) & 7;

    ASSERT( !getkey().is_conference() );

    options().unmasked().set( F_HISTORY_NEED_LOAD );

    ASSERT(!options().is(F_UNKNOWN));
}

bool contact_root_c::save(contacts_s * c) const
{
    if (getkey().is_conference())
        return false; // conferences never saved as contact; see table conferences

    c->metaid = 0;
    c->options = get_options() | (get_keeph() << 16) | (get_aaac() << 19) | ( get_mhtype() << 21 ) | ( get_imnb() << 24 );
    c->name = get_name(false);
    c->customname = get_customname();
    c->comment = get_comment();
    c->greeting = greeting;
    c->tags = get_tags();
    c->statusmsg = get_statusmsg(false);
    c->readtime = get_readtime();
    c->msghandler = ts::to_utf8( get_mhcmd() );
    // avatar data copied not here, see profile_c::set_avatar

    return true;
}

void contact_root_c::join_conference(contact_root_c *c)
{
    if (ASSERT(getkey().is_conference()))
    {
        ts::tmp_tbuf_t<contact_id_s> c2a;

        c->subiterate([&](contact_c *c) {
            if (getkey().protoid == c->getkey().protoid)
                c2a.add(c->getkey().gidcid());
        });

        if (c2a.count() == 0)
        {
            dialog_msgbox_c::mb_warning(TTT("Conference contacts must be from same network", 255)).summon(true);

        }
        else if (active_protocol_c *ap = prf().ap(getkey().protoid))
        {
            for (contact_id_s cid : c2a)
                ap->join_conference(getkey().gidcid(), cid);
        }
    }
}

conference_s *contact_root_c::find_conference()
{
    if (getkey().is_conference())
    {
        ASSERT( getkey().temp_type == TCT_CONFERENCE );
        return prf().find_conference_by_id( getkey().contactid );
    }
    return nullptr;
}


contact_root_c *contact_key_s::find_root_contact() const
{
    if ( is_meta() || TCT_CONFERENCE == temp_type )
        return contacts().rfind( *this );

    ASSERT( TCT_UNKNOWN_MEMBER != temp_type ); // unknown conference member has no root ( it is not conference because same unknown contact can be member of more then one conference )

    if ( is_conference() || is_conference_member() )
    {
        conference_s *c = prf().find_conference( contact_id_s(contact_id_s::CONFERENCE, gid), protoid );
        if (!c) return nullptr;
        return c->confa;
    }

    if ( contact_c * c = contacts().find( contact_key_s(contact_id_s(contact_id_s::CONTACT, contactid), protoid ) ) )
        return c->get_historian();

    return nullptr;
}

contact_c *contact_key_s::find_sub_contact() const
{
    if ( contactid > 0 )
    {
        if ( TCT_CONFERENCE_MEMBER == temp_type )
        {
            contact_id_s cid(contact_id_s::CONTACT, contactid);
            cid.confmember = 1;
            cid.unknown = 1;
            conference_member_s *cm = prf().find_conference_member( cid, protoid );
            if (!cm)
                return contacts().find( contact_key_s(contact_id_s(contact_id_s::CONTACT, contactid), protoid ) );

            return cm->memba;
        }

        if ( TCT_UNKNOWN_MEMBER == temp_type)
        {
            conference_member_s *cm = prf().find_conference_member_by_id( contactid );
            if ( !cm ) return nullptr;
            return cm->memba;
        }

        if (protoid > 0)
            return contacts().find( contact_key_s(contact_id_s(contact_id_s::CONTACT, contactid), protoid ) );
    }
    return nullptr;
}

contact_c *contact_key_s::find_conference_member( contact_root_c * confa ) const
{
    ASSERT( is_conference_member() );
    if ( conference_member_s *c = prf().find_conference_member(contact_id_s(contact_id_s::CONTACT, contactid), protoid) )
    {
        if ( confa && confa->subpresent( c->memba ) )
            return c->memba;
    }

    return nullptr;
}

uint64 operator |( const contact_key_s&root, const contact_key_s &sub )
{
    if (root.is_empty() && sub.is_conference())
    {
        uint64 r = 0x4000000000000000ull;

        // 7654321076543210765432107654321076543210765432107654321076543210
        // 01000000ggggggggggggggggpppppppppppppppp000000000000000000000000

        if ( TCT_CONFERENCE == sub.temp_type )
        {
            if ( conference_s *c = prf().find_conference_by_id( sub.contactid ) )
                r |= static_cast<uint64>( c->proto_key.id ) << 40;
        }
        else
            r |= static_cast<uint64>( sub.gid ) << 40;

        return r | ( static_cast<uint64>( sub.protoid ) << 24 );
    }

    ASSERT( root.is_conference() || ( root.is_meta() && !sub.is_conference_member() ) || root.is_empty() );
    ASSERT( !sub.is_conference() && !sub.is_empty() && !root.is_self && !sub.is_self );

    if ( root.is_empty() )
    {
        ASSERT( sub.temp_type == TCT_NONE );

        // 7654321076543210765432107654321076543210765432107654321076543210
        // 10100000ggggggggggggggggppppppppppppppppcccccccccccccccccccccccc

        if ( sub.is_conference_member() )
            return 0xA000000000000000ull | ( static_cast<uint64>( sub.gid ) << 40 ) | ( static_cast<uint64>( sub.protoid ) << 24 ) | static_cast<uint64>( sub.contactid );

        ASSERT( sub.contactid > 0 && sub.protoid > 0 );

        // 7654321076543210765432107654321076543210765432107654321076543210
        // 110000000000000000000000ppppppppppppppppcccccccccccccccccccccccc

        return 0xC000000000000000ull | ( static_cast<uint64>( sub.protoid ) << 24 ) | static_cast<uint64>( sub.contactid );

    }

    if (root.is_conference())
    {
        uint64 r = 0xA000000000000000ull;

        // 7654321076543210765432107654321076543210765432107654321076543210
        // 10100000ggggggggggggggggppppppppppppppppcccccccccccccccccccccccc

        if ( TCT_CONFERENCE == root.temp_type )
        {
            if ( conference_s *c = prf().find_conference_by_id( root.contactid ) )
                r |= static_cast<uint64>( c->proto_key.id ) << 40;
        } else
            r |= static_cast<uint64>( root.gid ) << 40;

        if ( TCT_UNKNOWN_MEMBER == sub.temp_type )
        {
            if (conference_member_s *cm = prf().find_conference_member_by_id( sub.contactid ))
                r |= static_cast<uint64>(cm->proto_key.id);

        } else
            r |= static_cast<uint64>( sub.contactid );

        return r | (static_cast<uint64>( root.protoid ) << 24);

    } else
    {
        // 7654321076543210765432107654321076543210765432107654321076543210
        // 110000000000000000000000ppppppppppppppppcccccccccccccccccccccccc

        // no need root id due sub is always in same root
        ASSERT( contacts().find(sub)->getmeta()->getkey() == root );
        return 0xC000000000000000ull | static_cast<uint64>( sub.contactid ) | ( static_cast<uint64>( sub.protoid ) << 24 );
    }
}

contact_id_s contact_key_s::gidcid() const
{
    if ( TCT_CONFERENCE == temp_type )
    {
        if ( conference_s *c = prf().find_conference_by_id( contactid ) )
            return ASSERT(c->proto_key.is_conference()) ? c->proto_key : contact_id_s();
        else
            return contact_id_s();
    }

    return gid ? contact_id_s(contact_id_s::CONFERENCE, gid) : contact_id_s(contact_id_s::CONTACT, contactid);
}
