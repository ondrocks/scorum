#include <boost/test/unit_test.hpp>

#include <scorum/protocol/betting/market.hpp>
#include <scorum/app/betting_api_impl.hpp>

#include <db_mock.hpp>

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/cxx11/is_sorted.hpp>

#include <hippomocks.h>

#include "object_wrapper.hpp"
#include "service_wrappers.hpp"

namespace betting_api_tests {

using namespace scorum;
using namespace scorum::chain;
using namespace scorum::protocol;
using namespace scorum::app;

using betting_api_impl = betting_api::impl;

class fixture : public shared_memory_fixture
{
public:
    MockRepository mocks;
    database* db_mock = mocks.Mock<database>();

    dba::db_accessor<betting_property_object> betting_prop_dba;
    dba::db_accessor<game_object> game_dba;
    dba::db_accessor<matched_bet_object> matched_bet_dba;
    dba::db_accessor<pending_bet_object> pending_bet_dba;

    fixture()
        : betting_prop_dba(*db_mock)
        , game_dba(*db_mock)
        , matched_bet_dba(*db_mock)
        , pending_bet_dba(*db_mock)
    {
    }
};

BOOST_AUTO_TEST_SUITE(betting_api_tests)

BOOST_FIXTURE_TEST_CASE(get_games_dont_throw, fixture)
{
    namespace dd = dba::detail;

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    std::vector<game_object> objects;

    mocks.ExpectCallFunc((dd::get_all_by<game_object, by_id>)).Return({ objects.begin(), objects.end() });

    BOOST_REQUIRE_NO_THROW(api.get_games_by_status({ game_status::resolved }));
}

struct get_game_winners_fixture : public fixture
{
    scorum::uuid_type uuid_ns = boost::uuids::string_generator()("00000000-0000-0000-0000-000000000001");
    boost::uuids::name_generator uuid_gen = boost::uuids::name_generator(uuid_ns);
};

BOOST_FIXTURE_TEST_CASE(unknown_uuid_should_throw, get_game_winners_fixture)
{
    mocks.ExpectCallFunc((dba::detail::is_exists_by<game_object, by_uuid, uuid_type>)).Return(false);

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    BOOST_CHECK_THROW(api.get_game_winners(uuid_gen("unknown")), fc::assert_exception);
}

BOOST_FIXTURE_TEST_CASE(non_finished_game_should_return_empty_result, get_game_winners_fixture)
{
    namespace dd = dba::detail;

    auto game_uuid = uuid_gen("game");
    auto game = create_object<game_object>(shm, [&](game_object& g) {
        g.id = 0;
        g.uuid = game_uuid;
        g.status = game_status::finished;
        g.results = { handicap::under{ 500 }, correct_score::yes{ 3, 3 }, goal_home::no{} };
    });

    mocks.ExpectCallFunc((dd::is_exists_by<game_object, by_uuid, uuid_type>)).Return(true);
    mocks.ExpectCallFunc((dd::get_by<game_object, by_uuid, uuid_type>)).With(_, game_uuid).ReturnByRef(game);
    mocks.ExpectCallFunc((dd::get_range_by<matched_bet_object, by_game_uuid_market, uuid_type>)).Return({});

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto winners = api.get_game_winners(game_uuid);
}

BOOST_FIXTURE_TEST_CASE(check_first_better_is_winner, get_game_winners_fixture)
{
    // Setup
    namespace dd = dba::detail;

    auto game_uuid = uuid_gen("game");
    auto game = create_object<game_object>(shm, [&](game_object& g) {
        g.uuid = game_uuid;
        g.status = game_status::finished;
        g.results = { handicap::under{ 500 } };
    });

    auto matched_bets = { create_object<matched_bet_object>(shm, [this](matched_bet_object& o) {
        o.market = handicap{ 500 };
        o.bet1_data = { uuid_gen("b1"), {}, "", handicap::over{ 500 } };
        o.bet2_data = { uuid_gen("b2"), {}, "", handicap::under{ 500 } };
    }) };

    mocks.OnCallFunc((dd::is_exists_by<game_object, by_uuid, uuid_type>)).Return(true);
    mocks.ExpectCallFunc((dd::get_by<game_object, by_uuid, uuid_type>)).With(_, game_uuid).ReturnByRef(game);
    mocks.ExpectCallFunc((dd::get_range_by<matched_bet_object, by_game_uuid_market, uuid_type>)).Return(matched_bets);

    // Run
    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto winners = api.get_game_winners(game_uuid);

    // Check
    BOOST_REQUIRE_EQUAL(winners.size(), 1u);

    BOOST_CHECK_EQUAL(winners[0].market.which(), (int)chain::market_type::tag<handicap>::value);
    BOOST_CHECK_EQUAL(winners[0].winner.wincase.which(), (int)chain::wincase_type::tag<handicap::under>::value);
    BOOST_CHECK_EQUAL(winners[0].loser.wincase.which(), (int)chain::wincase_type::tag<handicap::over>::value);
}

BOOST_FIXTURE_TEST_CASE(check_second_better_is_winner, get_game_winners_fixture)
{
    // Setup
    namespace dd = dba::detail;

    auto game_uuid = uuid_gen("game");
    auto game = create_object<game_object>(shm, [&](game_object& g) {
        g.uuid = game_uuid;
        g.status = game_status::finished;
        g.results = { handicap::over{ 500 } };
    });

    auto matched_bets = { create_object<matched_bet_object>(shm, [this](matched_bet_object& o) {
        o.market = handicap{ 500 };
        o.bet1_data = { uuid_gen("b1"), {}, "", handicap::over{ 500 } };
        o.bet2_data = { uuid_gen("b2"), {}, "", handicap::under{ 500 } };
    }) };

    mocks.OnCallFunc((dd::is_exists_by<game_object, by_uuid, uuid_type>)).Return(true);
    mocks.ExpectCallFunc((dd::get_by<game_object, by_uuid, uuid_type>)).With(_, game_uuid).ReturnByRef(game);
    mocks.ExpectCallFunc((dd::get_range_by<matched_bet_object, by_game_uuid_market, uuid_type>)).Return(matched_bets);

    // Run
    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto winners = api.get_game_winners(game_uuid);

    // Check
    BOOST_REQUIRE_EQUAL(winners.size(), 1u);

    BOOST_CHECK_EQUAL(winners[0].market.which(), (int)chain::market_type::tag<handicap>::value);
    BOOST_CHECK_EQUAL(winners[0].winner.wincase.which(), (int)chain::wincase_type::tag<handicap::over>::value);
    BOOST_CHECK_EQUAL(winners[0].loser.wincase.which(), (int)chain::wincase_type::tag<handicap::under>::value);
}

BOOST_FIXTURE_TEST_CASE(check_all_data_present_in_result, get_game_winners_fixture)
{
    market_type market = handicap{ 500 };
    bet_data winner = { uuid_gen("b1"), {}, "b1", handicap::over{ 500 }, ASSET_SCR(1000) };
    bet_data loser = { uuid_gen("b2"), {}, "b2", handicap::under{ 500 }, ASSET_SCR(500) };

    winner_api_object obj(market, winner, loser);

    BOOST_CHECK_EQUAL(obj.market.which(), (int)chain::market_type::tag<handicap>::value);
    BOOST_CHECK_EQUAL(obj.profit.amount, 500u);
    BOOST_CHECK_EQUAL(obj.income.amount, 1500u);
    BOOST_CHECK_EQUAL(obj.winner.wincase.which(), (int)chain::wincase_type::tag<handicap::over>::value);
    BOOST_CHECK_EQUAL(boost::uuids::to_string(obj.winner.uuid), boost::uuids::to_string(uuid_gen("b1")));
    BOOST_CHECK_EQUAL(obj.winner.name, "b1");
    BOOST_CHECK_EQUAL(obj.loser.wincase.which(), (int)chain::wincase_type::tag<handicap::under>::value);
    BOOST_CHECK_EQUAL(boost::uuids::to_string(obj.loser.uuid), boost::uuids::to_string(uuid_gen("b2")));
    BOOST_CHECK_EQUAL(obj.loser.name, "b2");
}

BOOST_FIXTURE_TEST_CASE(trd_state_markets_without_winner_are_not_returned, get_game_winners_fixture)
{
    // Setup
    namespace dd = dba::detail;

    auto uuid_gen = boost::uuids::random_generator();

    auto game_uuid = uuid_gen();
    auto game = create_object<game_object>(shm, [&](game_object& g) {
        g.uuid = game_uuid;
        g.status = game_status::finished;
        g.results = { handicap::under{ 0 }, correct_score::yes{ 3, 3 } };
    });
    // clang-format off
    auto matched_bets = {
        create_object<matched_bet_object>(shm, [&](matched_bet_object& o) {
            o.market = handicap{ 0 };
            o.bet1_data = { uuid_gen(), {}, "", handicap::over{ 0 } };
            o.bet2_data = { uuid_gen(), {}, "", handicap::under{ 0 } }; // winner
        }),
        create_object<matched_bet_object>(shm, [&](matched_bet_object& o) { // no result in game_object for this one
            o.market = handicap{1000};
            o.bet1_data = { uuid_gen(), {}, "", handicap::over{ 1000 } };
            o.bet2_data = { uuid_gen(), {}, "", handicap::under{ 1000 } };
        }),
        create_object<matched_bet_object>(shm, [&](matched_bet_object& o) {
            o.market = correct_score{ 3, 3 };
            o.bet1_data = { uuid_gen(), {}, "", correct_score::yes{ 3, 3, } }; // winner
            o.bet2_data = { uuid_gen(), {}, "", correct_score::no{ 3, 3 } };
        }),
        create_object<matched_bet_object>(shm, [&](matched_bet_object& o) { // no result in game_object for this one
            o.market = total{ 2000 };
            o.bet1_data = { uuid_gen(), {}, "", total::over{ 2000 } };
            o.bet2_data = { uuid_gen(), {}, "", total::under{ 2000 } };
        })
    };
    BOOST_REQUIRE(boost::algorithm::is_sorted(matched_bets, [](const auto& l, const auto& r) { return l.market < r.market; }));
    // clang-format on

    mocks.OnCallFunc((dd::is_exists_by<game_object, by_uuid, uuid_type>)).Return(true);
    mocks.ExpectCallFunc((dd::get_by<game_object, by_uuid, uuid_type>)).With(_, game_uuid).ReturnByRef(game);
    mocks.ExpectCallFunc((dd::get_range_by<matched_bet_object, by_game_uuid_market, uuid_type>)).Return(matched_bets);

    // Run
    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto winners = api.get_game_winners(game_uuid);

    // Check
    BOOST_REQUIRE_EQUAL(winners.size(), 2u);

    BOOST_CHECK_EQUAL(winners[0].market.which(), (int)chain::market_type::tag<handicap>::value);
    BOOST_CHECK_EQUAL(winners[0].market.get<handicap>().threshold, 0);
    BOOST_CHECK_EQUAL(winners[1].market.which(), (int)chain::market_type::tag<correct_score>::value);
}

struct get_games_fixture : public fixture
{
    get_games_fixture()
    {
        objects.push_back(
            create_object<game_object>(shm, [&](game_object& game) { game.status = game_status::created; }));

        objects.push_back(
            create_object<game_object>(shm, [&](game_object& game) { game.status = game_status::started; }));

        objects.push_back(
            create_object<game_object>(shm, [&](game_object& game) { game.status = game_status::finished; }));

        objects.push_back(
            create_object<game_object>(shm, [&](game_object& game) { game.status = game_status::resolved; }));

        objects.push_back(
            create_object<game_object>(shm, [&](game_object& game) { game.status = game_status::expired; }));

        objects.push_back(
            create_object<game_object>(shm, [&](game_object& game) { game.status = game_status::cancelled; }));
    }

    std::vector<game_object> objects;
};

BOOST_FIXTURE_TEST_CASE(get_games_return_all_games_in_creation_order, get_games_fixture)
{
    namespace dd = dba::detail;

    mocks.ExpectCallFunc((dd::get_all_by<game_object, by_id>)).Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    std::vector<game_api_object> games
        = api.get_games_by_status({ game_status::started, game_status::created, game_status::finished,
                                    game_status::cancelled, game_status::expired, game_status::resolved });

    BOOST_CHECK_EQUAL(games.size(), 6u);
    BOOST_CHECK(games[0].status == game_status::created);
    BOOST_CHECK(games[1].status == game_status::started);
    BOOST_CHECK(games[2].status == game_status::finished);
    BOOST_CHECK(games[3].status == game_status::resolved);
    BOOST_CHECK(games[4].status == game_status::expired);
    BOOST_CHECK(games[5].status == game_status::cancelled);
}

BOOST_FIXTURE_TEST_CASE(return_games_with_created_status, get_games_fixture)
{
    namespace dd = dba::detail;

    mocks.ExpectCallFunc((dd::get_all_by<game_object, by_id>)).Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    std::vector<game_api_object> games = api.get_games_by_status({ game_status::created });

    BOOST_REQUIRE_EQUAL(games.size(), 1);
    BOOST_CHECK(games[0].status == game_status::created);
}

BOOST_FIXTURE_TEST_CASE(return_games_with_started_status, get_games_fixture)
{
    namespace dd = dba::detail;

    mocks.ExpectCallFunc((dd::get_all_by<game_object, by_id>)).Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    std::vector<game_api_object> games = api.get_games_by_status({ game_status::started });

    BOOST_REQUIRE_EQUAL(games.size(), 1);
    BOOST_CHECK(games[0].status == game_status::started);
}

BOOST_FIXTURE_TEST_CASE(return_games_with_finished_status, get_games_fixture)
{
    namespace dd = dba::detail;

    mocks.ExpectCallFunc((dd::get_all_by<game_object, by_id>)).Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    std::vector<game_api_object> games = api.get_games_by_status({ game_status::finished });

    BOOST_REQUIRE_EQUAL(games.size(), 1);
    BOOST_CHECK(games[0].status == game_status::finished);
}

BOOST_FIXTURE_TEST_CASE(return_games_with_created_finished_cancelled_status, get_games_fixture)
{
    namespace dd = dba::detail;

    mocks.ExpectCallFunc((dd::get_all_by<game_object, by_id>)).Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    std::vector<game_api_object> games
        = api.get_games_by_status({ game_status::finished, game_status::created, game_status::cancelled });

    BOOST_REQUIRE_EQUAL(games.size(), 3);
    BOOST_CHECK(games[0].status == game_status::created);
    BOOST_CHECK(games[1].status == game_status::finished);
    BOOST_CHECK(games[2].status == game_status::cancelled);
}

BOOST_FIXTURE_TEST_CASE(return_two_games_with_finished_status, get_games_fixture)
{
    namespace dd = dba::detail;

    objects.push_back(create_object<game_object>(shm, [&](game_object& game) { game.status = game_status::finished; }));

    mocks.ExpectCallFunc((dd::get_all_by<game_object, by_id>)).Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    std::vector<game_api_object> games = api.get_games_by_status({ game_status::finished });

    BOOST_REQUIRE_EQUAL(games.size(), 2);
    BOOST_CHECK(games[0].status == game_status::finished);
    BOOST_CHECK(games[1].status == game_status::finished);
}

BOOST_FIXTURE_TEST_CASE(throw_exception_when_limit_is_negative, get_games_fixture)
{
    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    BOOST_REQUIRE_THROW(api.lookup_pending_bets(0, -1), fc::assert_exception);
    BOOST_REQUIRE_THROW(api.lookup_matched_bets(0, -1), fc::assert_exception);
}

BOOST_FIXTURE_TEST_CASE(throw_exception_when_limit_gt_than_max_limit, get_games_fixture)
{
    const auto max_limit = 100;

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba, max_limit);

    BOOST_REQUIRE_THROW(api.lookup_pending_bets(0, max_limit + 1), fc::assert_exception);
    BOOST_REQUIRE_THROW(api.lookup_matched_bets(0, max_limit + 1), fc::assert_exception);
}

BOOST_FIXTURE_TEST_CASE(dont_throw_when_limit_is_zero, get_games_fixture)
{
    namespace dd = dba::detail;

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    std::vector<pending_bet_object> pbets;
    std::vector<matched_bet_object> mbets;

    mocks.OnCallFunc((dd::get_range_by<pending_bet_object, by_id, pending_bet_id_type>))
        .Return({ pbets.begin(), pbets.end() });
    mocks.OnCallFunc((dd::get_range_by<matched_bet_object, by_id, matched_bet_id_type>))
        .Return({ mbets.begin(), mbets.end() });

    BOOST_REQUIRE_NO_THROW(api.lookup_pending_bets(0, 0));
    BOOST_REQUIRE_NO_THROW(api.lookup_matched_bets(0, 0));
}

BOOST_FIXTURE_TEST_CASE(dont_throw_when_limit_eq_max, get_games_fixture)
{
    namespace dd = dba::detail;

    const auto max_limit = 100;

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba, max_limit);

    std::vector<pending_bet_object> pbets;
    std::vector<matched_bet_object> mbets;

    mocks.OnCallFunc((dd::get_range_by<pending_bet_object, by_id, pending_bet_id_type>))
        .Return({ pbets.begin(), pbets.end() });
    mocks.OnCallFunc((dd::get_range_by<matched_bet_object, by_id, matched_bet_id_type>))
        .Return({ mbets.begin(), mbets.end() });

    BOOST_REQUIRE_NO_THROW(api.lookup_pending_bets(0, max_limit));
    BOOST_REQUIRE_NO_THROW(api.lookup_matched_bets(0, max_limit));
}

template <typename T> struct get_bets_fixture : public fixture
{
    get_bets_fixture()
    {
        objects.push_back(create_object<T>(shm, [&](auto& bet) { bet.id = 0; }));
        objects.push_back(create_object<T>(shm, [&](auto& bet) { bet.id = 1; }));
        objects.push_back(create_object<T>(shm, [&](auto& bet) { bet.id = 2; }));
    }

    std::vector<T> objects;
};

BOOST_FIXTURE_TEST_CASE(check_get_pending_bets_from_arg, get_bets_fixture<pending_bet_object>)
{
    namespace dd = dba::detail;

    mocks.OnCallFunc((dd::get_range_by<pending_bet_object, by_id, pending_bet_id_type>))
        .Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    api.lookup_pending_bets(0, 1);
}

BOOST_FIXTURE_TEST_CASE(get_one_pending_bet, get_bets_fixture<pending_bet_object>)
{
    namespace dd = dba::detail;

    mocks.OnCallFunc((dd::get_range_by<pending_bet_object, by_id, pending_bet_id_type>))
        .Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    auto bets = api.lookup_pending_bets(0, 1);

    BOOST_REQUIRE_EQUAL(bets.size(), 1);

    BOOST_CHECK(bets[0].id == 0u);
}

BOOST_FIXTURE_TEST_CASE(get_all_pending_bets, get_bets_fixture<pending_bet_object>)
{
    namespace dd = dba::detail;

    mocks.OnCallFunc((dd::get_range_by<pending_bet_object, by_id, pending_bet_id_type>))
        .Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    auto bets = api.lookup_pending_bets(0, 100);

    BOOST_REQUIRE_EQUAL(bets.size(), 3);

    BOOST_CHECK(bets[0].id == 0u);
    BOOST_CHECK(bets[1].id == 1u);
    BOOST_CHECK(bets[2].id == 2u);
}

BOOST_FIXTURE_TEST_CASE(check_get_matched_bets_from_arg, get_bets_fixture<matched_bet_object>)
{
    namespace dd = dba::detail;

    mocks.OnCallFunc((dd::get_range_by<matched_bet_object, by_id, matched_bet_id_type>))
        .Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    api.lookup_matched_bets(0, 1);
}

BOOST_FIXTURE_TEST_CASE(get_one_matched_bet, get_bets_fixture<matched_bet_object>)
{
    namespace dd = dba::detail;

    mocks.OnCallFunc((dd::get_range_by<matched_bet_object, by_id, matched_bet_id_type>))
        .Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    auto bets = api.lookup_matched_bets(0, 1);

    BOOST_REQUIRE_EQUAL(bets.size(), 1);

    BOOST_CHECK(bets[0].id == 0u);
}

BOOST_FIXTURE_TEST_CASE(get_all_matched_bets, get_bets_fixture<matched_bet_object>)
{
    namespace dd = dba::detail;

    mocks.OnCallFunc((dd::get_range_by<matched_bet_object, by_id, matched_bet_id_type>))
        .Return({ objects.begin(), objects.end() });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);
    auto bets = api.lookup_matched_bets(0, 100);

    BOOST_REQUIRE_EQUAL(bets.size(), 3);

    BOOST_CHECK(bets[0].id == 0u);
    BOOST_CHECK(bets[1].id == 1u);
    BOOST_CHECK(bets[2].id == 2u);
}

BOOST_AUTO_TEST_SUITE_END()

class betting_api_fixture
{
public:
    db_mock db;

    dba::db_accessor<betting_property_object> betting_prop_dba;
    dba::db_accessor<game_object> game_dba;
    dba::db_accessor<matched_bet_object> matched_bet_dba;
    dba::db_accessor<pending_bet_object> pending_bet_dba;

    uuid_type uuid_ns = boost::uuids::string_generator()("e629f9aa-6b2c-46aa-8fa8-36770e7a7a5f");
    boost::uuids::name_generator uuid_gen = boost::uuids::name_generator(uuid_ns);

    betting_api_fixture()
        : betting_prop_dba(db)
        , game_dba(db)
        , matched_bet_dba(db)
        , pending_bet_dba(db)
    {
        db.add_index<betting_property_index>();
        db.add_index<game_index>();
        db.add_index<pending_bet_index>();
        db.add_index<matched_bet_index>();
    }
};

BOOST_FIXTURE_TEST_SUITE(get_games_betting_api_tests, betting_api_fixture)

BOOST_AUTO_TEST_CASE(empty_uuids_list_should_return_empty)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_games_by_uuids({});

    BOOST_REQUIRE_EQUAL(result.size(), 0u);
}

BOOST_AUTO_TEST_CASE(non_exists_uuid_should_return_empty)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_games_by_uuids({ uuid_gen("b1") });

    BOOST_REQUIRE_EQUAL(result.size(), 0u);
}

BOOST_AUTO_TEST_CASE(passed_uuids_is_superset_should_return_in_correct_order)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b1"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_games_by_uuids({ uuid_gen("b2"), uuid_gen("b1"), uuid_gen("b0") });

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0].uuid, uuid_gen("b1"));
    BOOST_CHECK_EQUAL(result[1].uuid, uuid_gen("b0"));
}

BOOST_AUTO_TEST_CASE(passed_uuids_is_subset_should_return_in_correct_order)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b1"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b2"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_games_by_uuids({ uuid_gen("b1"), uuid_gen("b2") });

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0].uuid, uuid_gen("b1"));
    BOOST_CHECK_EQUAL(result[1].uuid, uuid_gen("b2"));
}

BOOST_AUTO_TEST_CASE(get_by_uuids_empty_db_should_return_empty)
{
    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_games_by_uuids({ uuid_gen("b1"), uuid_gen("b2") });

    BOOST_REQUIRE_EQUAL(result.size(), 0u);
}

BOOST_AUTO_TEST_CASE(return_all_starting_from_the_beginning)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b1"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.lookup_games_by_id(0, 42);

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
}

BOOST_AUTO_TEST_CASE(return_the_tail_starting_from_the_middle)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b1"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b2"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b3"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b4"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.lookup_games_by_id(2, 42);

    BOOST_REQUIRE_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result[0].uuid, uuid_gen("b2"));
    BOOST_CHECK_EQUAL(result[1].uuid, uuid_gen("b3"));
    BOOST_CHECK_EQUAL(result[2].uuid, uuid_gen("b4"));
}

BOOST_AUTO_TEST_CASE(limit_test)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b1"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b2"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b3"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b4"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.lookup_games_by_id(1, 2);

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0].uuid, uuid_gen("b1"));
    BOOST_CHECK_EQUAL(result[1].uuid, uuid_gen("b2"));
}

BOOST_AUTO_TEST_CASE(api_lookup_limit_is_less_than_limit)
{
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b0"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b1"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b2"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b3"); });
    db.create<game_object>([&](game_object& o) { o.uuid = uuid_gen("b4"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba, 2);

    auto result = api.lookup_games_by_id(1, 3);

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0].uuid, uuid_gen("b1"));
    BOOST_CHECK_EQUAL(result[1].uuid, uuid_gen("b2"));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(bet_bets_betting_api_tests, betting_api_fixture)

BOOST_AUTO_TEST_CASE(get_pending_bets_test_passed_uuids_is_subset)
{
    db.create<pending_bet_object>([&](pending_bet_object& o) { o.data.uuid = uuid_gen("b0"); });
    db.create<pending_bet_object>([&](pending_bet_object& o) { o.data.uuid = uuid_gen("b1"); });
    db.create<pending_bet_object>([&](pending_bet_object& o) { o.data.uuid = uuid_gen("b2"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_pending_bets({ uuid_gen("b1"), uuid_gen("b2") });

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0].data.uuid, uuid_gen("b1"));
    BOOST_CHECK_EQUAL(result[1].data.uuid, uuid_gen("b2"));
}

BOOST_AUTO_TEST_CASE(get_pending_bets_test_passed_uuids_is_superset)
{
    db.create<pending_bet_object>([&](pending_bet_object& o) { o.data.uuid = uuid_gen("b0"); });
    db.create<pending_bet_object>([&](pending_bet_object& o) { o.data.uuid = uuid_gen("b1"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_pending_bets({ uuid_gen("b0"), uuid_gen("uknown0"), uuid_gen("b1"), uuid_gen("uknown1") });

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
    BOOST_CHECK_EQUAL(result[0].data.uuid, uuid_gen("b0"));
    BOOST_CHECK_EQUAL(result[1].data.uuid, uuid_gen("b1"));
}

BOOST_AUTO_TEST_CASE(get_pending_bets_test_passed_uuids_is_empty)
{
    db.create<pending_bet_object>([&](pending_bet_object& o) { o.data.uuid = uuid_gen("b0"); });

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_pending_bets({});

    BOOST_REQUIRE_EQUAL(result.size(), 0u);
}

BOOST_AUTO_TEST_CASE(get_pending_bets_test_empty_db)
{
    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_pending_bets({ uuid_gen("b1"), uuid_gen("b2") });

    BOOST_REQUIRE_EQUAL(result.size(), 0u);
}

BOOST_AUTO_TEST_CASE(get_matched_bets_no_duplicates_check)
{
    // clang-format off
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b0"); o.bet2_data.uuid = uuid_gen("b1"); });
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b2"); o.bet2_data.uuid = uuid_gen("b1"); });
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b0"); o.bet2_data.uuid = uuid_gen("b3"); });
    // clang-format on

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_matched_bets({ uuid_gen("b3"), uuid_gen("b0") });

    BOOST_REQUIRE_EQUAL(result.size(), 2u);
    boost::sort(result, [](const auto& l, const auto& r) { return l.id < r.id; });
    BOOST_CHECK_EQUAL(result[0].id._id, 0);
    BOOST_CHECK_EQUAL(result[1].id._id, 2);
}

BOOST_AUTO_TEST_CASE(get_matched_bets_same_better_several_bets_should_return)
{
    // clang-format off
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b0"); o.bet2_data.uuid = uuid_gen("b1"); });
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b2"); o.bet2_data.uuid = uuid_gen("b0"); });
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b3"); o.bet2_data.uuid = uuid_gen("b4"); });
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b5"); o.bet2_data.uuid = uuid_gen("b1"); });
    // clang-format on

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_matched_bets({ uuid_gen("b1"), uuid_gen("b0") });

    BOOST_REQUIRE_EQUAL(result.size(), 3u);
    boost::sort(result, [](const auto& l, const auto& r) { return l.id < r.id; });
    BOOST_CHECK_EQUAL(result[0].id._id, 0);
    BOOST_CHECK_EQUAL(result[1].id._id, 1);
    BOOST_CHECK_EQUAL(result[2].id._id, 3);
}

BOOST_AUTO_TEST_CASE(get_matched_bets_test_passed_uuids_is_empty)
{
    // clang-format off
    db.create<matched_bet_object>([&](matched_bet_object& o) { o.bet1_data.uuid = uuid_gen("b0"); o.bet2_data.uuid = uuid_gen("b1"); });
    // clang-format on

    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_matched_bets({});

    BOOST_REQUIRE_EQUAL(result.size(), 0u);
}

BOOST_AUTO_TEST_CASE(get_matched_bets_test_empty_db)
{
    betting_api_impl api(betting_prop_dba, game_dba, matched_bet_dba, pending_bet_dba);

    auto result = api.get_matched_bets({ uuid_gen("b1"), uuid_gen("b2") });

    BOOST_REQUIRE_EQUAL(result.size(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace betting_api_tests
