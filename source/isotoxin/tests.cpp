#include "isotoxin.h"
#include "vector"

//-V::813

#ifndef _FINAL

#if 0
struct txxx
{
    int a;
    txxx(int a) :a(a) {}
    ~txxx()
    {
        WARNING("%i", a);
    }
};

struct tests
{
    MOVABLE(false);
    DUMMY(tests);
    txxx *d;
    tests() :d(0) {}
    ~tests()
    {
        if (d) TSDEL(d); else WARNING("NULL");
    }

    tests(const tests & o)
    {
        d = nullptr;
    }
    //tests( tests && o)
    //{
    //    d = o.d;
    //    o.d = nullptr;
    //}

};
#endif

ts::random_modnar_c r1, r2;

template< size_t B, size_t CNT, typename RVT > void t( ts::packed_buf_c<B,CNT,RVT> & b )
{
    uint seed = r1.get_next();
    r2.init(seed);

    for(int i=0;i<CNT;++i)
    {
        RVT v = (RVT)r2.get_next( 1<<B );
        b.set( i, v );
    }

    r2.init(seed);

    for (int i = 0; i < CNT; ++i)
    {
        RVT v = (RVT)r2.get_next(1<<B);
        RVT vc = b.get(i);
        ASSERT(v == vc, "ts::packed_buf_c");
    }
}

void crash()
{
    //*((int *)1) = 1;
}

isotoxin_ipc_s *ipcx;
int nnnn = 0;
int npersec = 0;
bool cmdhandlerx(ipcr r)
{
    switch (r.header().cmd)
    {
        case XX_PONG:
        {
            ++nnnn;
            if (nnnn >= 5000) return false;

            ts::uint8 data[16384];
            memset(data, 1, sizeof(data));
            ipcx->send(ipcw(XX_PING) << data_block_s(data,sizeof(data)));

        }
    }
    return true;
}

bool tickx()
{
    return true;
}

void test_ipc()
{
    isotoxin_ipc_s ipcs(ts::str_c(CONSTASTR("testtest")), cmdhandlerx);
    if (ipcs.ipc_ok)
    {
        ts::Time stime = ts::Time::current();
        ts::uint8 data[16384];
        memset(data,1,sizeof(data));
        ipcx = &ipcs; //-V506
        ipcs.send(ipcw(XX_PING) << data_block_s(data,sizeof(data)));
        ipcs.wait_loop(tickx);

        ts::Time::update_thread_time();
        npersec = nnnn * 1000 / ( ts::Time::current() - stime);
        DEBUG_BREAK();
    }
}

void test_cairo()
{
    //ts::bitmap_c bmp;
    //bmp.create_ARGB( ts::ivec2(512,512) );
    //cairo_surface_t *s = cairo_image_surface_create_for_data( bmp.body(), CAIRO_FORMAT_ARGB32, bmp.info().sz.x, bmp.info().sz.y, bmp.info().pitch );
    //cairo_t *c = cairo_create(s);
    //cairo_surface_destroy(s);
    //
    //cairo_destroy(c);

    ts::buf0_c b; b.load_from_text_file(CONSTWSTR("process.svg"));
    char *testsvg = (char *)b.data();

    rsvg_svg_c *n = rsvg_svg_c::build_from_xml(testsvg);
    if (n)
    {
        ts::bitmap_c bmp;
        bmp.create_ARGB( n->size() );

        bmp.fill( ts::ARGB(45,66,78) );
        n->render( bmp.extbody() );
        bmp.save_as_png( CONSTWSTR("svg_c.png") );

        bmp.fill(0);
        n->render(bmp.extbody());

        ts::bitmap_c bsave;
        bsave.create_ARGB(bmp.info().sz);
        bsave.fill(0xffffffff);
        //bsave.alpha_blend(ts::ivec2(0), bmp);
        ts::img_helper_alpha_blend_pm(bsave.body(), bsave.info().pitch, bmp.body(), bmp.info(), 255);
        bsave.save_as_png(CONSTWSTR("svg_w.png"));

        n->release();
    }
}

void dotests0()
{
    //test_cairo();
}

void dotests()
{
    //test_ipc();

    /*
    ts::bitmap_c basei; basei.load_from_file(L"1\\ava.png");
    ts::bitmap_c img; img.load_from_file(L"1\\creambee-qrcode.png");
    ts::bitmap_c b;
    b.create_RGB(basei.info().sz);
    b.alpha_blend(ts::ivec2(17,19), img.extbody(), basei.extbody());
    b.save_as_PNG(L"1\\out.png");
    */

    //ts::buf_c yuv; yuv.load_from_disk_file(L"i420.bin");
    //ts::bitmap_c bmp;
    //bmp.create_RGBA( ts::ivec2(1920,1200) );
    //bmp.convert_from_yuv( ts::ivec2(0), bmp.info().sz, yuv.data(), ts::YFORMAT_I420 );
    //bmp.save_as_png( L"out.png" );

    //uint64 u1 = prf().getuid();
    //uint64 u2 = prf().getuid();
    //uint64 u3 = prf().getuid();

    // test packed_buf_c

    //ts::packed_buf_c<20, 121, int> b0;

    //{
    //    ts::buf_c b;
    //    b.load_from_disk_file(L"C:\\2\\1\\nodes");
    //    //b.load_from_disk_file(L"C:\\2\\1\\json1");
    //    ts::json_c jsn;
    //    int n = jsn.parse( b.cstr() );
    //    DEBUG_BREAK();
    //}




    ts::packed_buf_c<1,121> b1;
    ts::packed_buf_c<2,113> b2;
    ts::packed_buf_c<3,97> b3;
    ts::packed_buf_c<4,3> b4;
    ts::packed_buf_c<5,3> b5;
    ts::packed_buf_c<6,3> b6;
    ts::packed_buf_c<7,72> b7;
    ts::packed_buf_c<8,11> b8;
    ts::packed_buf_c<9,1311> b9;
    ts::packed_buf_c<1,1> b10;
    ts::packed_buf_c<9,2> b11;
    ts::packed_buf_c<10,2> b12;
    t(b1);
    t(b2);
    t(b3);
    t(b4);
    t(b5);
    t(b6);
    t(b7);
    t(b8);
    t(b9);
    t(b10);
    t(b11);
    t(b12);

    //ARRAY_TYPE_INPLACE( draw_data_s, 4 ) ta;
    //ta.duplast();
    //ta.duplast();
    //ta.duplast();
    //ta.duplast();
    //ta.duplast();
    //ta.truncate( ta.size() - 1 );
    //ta.truncate( ta.size() - 1 );
    //ta.truncate( ta.size() - 1 );
    //ta.truncate( ta.size() - 1 );

    //rectengine_c h1, h2;
    //ARRAY_TYPE_SAFE(sqhandler_i,1) iii;
    //iii.set(2, &h1);
    //iii.set(7, &h2);

    //int sum = 0;
    //for( sqhandler_i *el : iii )
    //{
    //    sum += (int)el;
    //}

   // DEBUG_BREAK();

#if 0
    {
        ts::uint8 bbb[sizeof(tests)];
        tests *jjj = (tests *)&bbb;
        tests kkk; kkk.d = TSNEW(txxx, 111);
        TSPLACENEW(jjj,kkk);



        ARRAY_TYPE_INPLACE(tests, 4) tar;
        tar.add().d = TSNEW(txxx, 11);
        tar.add().d = TSNEW(txxx, 12);
        tar.add().d = TSNEW(txxx, 13);
        tar.add().d = TSNEW(txxx, 14);
        tar.add().d = TSNEW(txxx, 15);
        tests tt = tar.get_remove_slow(0);
        tar.add().d = TSNEW(txxx, 16);


    }

    DEBUG_BREAK();
#endif

	/*
	//m_cmdpars.qsplit( cmdl );
	//__int64 ggg = ts::zstrings_internal::maximum<uint>::value;

	const ts::wchar *ttt = L"-1123.171a";
	double xxx;
	bool ok = ts::CHARz_to_double(xxx,ttt,7);

    ts::vec2 v(1.3,2.7);
    ts::ivec3 v3(3,7,11);

    ts::aligned<double> ad(53.0);

	ts::wstr_c s(L"-55,12");
	ts::wstr_c x(s);
	ts::str_c z("ansi");
    ts::str_c zzz;

	int r = sizeof(z);

	//s.insert(0,L"nnn");
	int t1;
	double t2;
	//ts::wstr_c t1, t2;
	s.split(t1,t2,",");
	s.set_as_utf8("abc");
	//z = s;

	ts::swstr_c s1(z);
	z += s;
	ts::swstr_t<16> s2(s1);
	ts::sstr_t<32> s3(s2);
	ZSTRINGS_WIDECHAR *yyy=(ZSTRINGS_WIDECHAR *)&s2;

    ts::str_t<char, ts::str_core_static_c<char, 32> > hhh("nnff");

	s1 += s2;
	s1 += z;


	z = s1 + s2;
	s1 = z + s2;

	//s1 = z;
	s2 = x;

    ts::strings_c<ZSTRINGS_WIDECHAR> sa1;
    sa1.add(L"s1");
    sa1.add(L"s2");
    sa1.add(L"s3");

	ts::type_buf_c<int> bbb;
	bbb.add(12);
	bbb.add(13);

	ts::hashmap_t<int, int> hm;
	hm[1] = 2;
	hm[3] = 4;

	DEBUG_BREAK();
	*/

    crash();

}

class test_window_2 : public gui_isodialog_c
{
    typedef gui_isodialog_c super;

protected:
    // /*virtual*/ int unique_tag() { return UD_NOT_UNIQUE; }
    /*virtual*/ void created() override
    {
        set_theme_rect(CONSTASTR("main"), false);
        super::created();
    }

public:
    test_window_2(initial_rect_data_s &data) :gui_isodialog_c(data) {}
    ~test_window_2() {}

    /*virtual*/ ts::ivec2 get_min_size() const { return ts::ivec2(150, 150); }

    //sqhandler_i
    /*virtual*/ bool sq_evt(system_query_e qp, RID rid, evt_data_s &data) override
    {
        if (qp == SQ_PARENT_RECT_CHANGED)
        {
            return false;
        }

        return super::sq_evt(qp, rid, data);
    }

    void getbutton(bcreate_s &bcr)
    {
        if (bcr.tag == 0)
        {
            super::getbutton(bcr);
        }
    }

    /*virtual*/ void on_confirm() override
    {
    }
};




bool zero_version = false;


void summon_test_window()
{
    //cfg().autoupdate_next( now() + 2 );
    //cfg().autoupdate_newver(CONSTASTR("0.0.0"), false);
    //zero_version = true;
    //gmsg<ISOGM_NEWVERSION> *m = TSNEW(gmsg<ISOGM_NEWVERSION>, CONSTASTR("0.1.225"));
    //m->send_to_main_thread();


    drawcollector dch;
    //RID r = MAKE_ROOT<test_window_2>();
    //MODIFY(r).allow_move_resize().size(502,447).setcenterpos().visible(true);

    //SUMMON_DIALOG<dialog_avaselector_c>(UD_AVASELECTOR, dialog_avasel_params_s(0));
    //SUMMON_DIALOG<dialog_prepareimage_c>(UD_PREPARE_IMAGE, contact_key_s());
    //SUMMON_DIALOG<dialog_colors_c>(UD_NOT_UNIQUE);

    //MAKE_ROOT<incoming_call_panel_c>( &contacts().get(1) );




    //static int n;
    //++n;

    //post_s p;
    //p.recv_time = now() - 2100;
    //p.set_message_text(
    //    ts::to_utf8( L"унделиверед www.microsoft.com\nsdfsddddf :rage:\nsdap sdpf posdf opewpo epor ewpor poewir poewi rpoew rpoqiwepro ifdsapof paosdflk ajflkjewo fk sdjfasdfds\n" )
    //    .append_as_int( n )
    //);

    //p.utag = prf().getuid();


    //MAKE_ROOT<incoming_msg_panel_c>( &contacts().get_self(), contacts().get_self().subget(0), p );


    //prf().test();

}







#endif
