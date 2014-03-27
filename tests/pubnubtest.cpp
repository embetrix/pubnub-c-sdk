
#include "gtest.h"

#include <curl/curl.h>
#include <fcntl.h>


namespace Test {

#include "../libpubnub/crypto.h"
#include "../libpubnub/pubnub.h"
#include "../libpubnub/pubnub-priv.h"

#undef PUBNUB_API
#define PUBNUB_API

static bool curlInit = false;


CURLM *curl_multi_init(void)
{
	curlInit = true;
	return ::curl_multi_init();
}

CURLMsg *curl_multi_info_read( CURLM *multi_handle,   int *msgs_in_queue)
{
	return NULL;
}

CURLMcode curl_multi_socket_action(CURLM * multi_handle,
                                    curl_socket_t sockfd, int ev_bitmask,
                                    int *running_handles)
{
	return CURLM_OK;
}

#include "../libpubnub/pubnub.c"


class PubnubTest : public ::testing::Test
{
protected:
	pubnub_callbacks cb;
	pubnub *p;
	char *url;

	int _err_pipe[2];
	int _old_err;
	std::string _err;

	static int addSock;
	static int addSockMode;

	static void
	pubnub_test_add_socket(struct pubnub *p, void *ctx_data, int fd, int mode,
			void (*cb)(struct pubnub *p, int fd, int mode, void *cb_data), void *cb_data)
	{
		addSock = fd;
		addSockMode = mode;
	}

	static int remSock;

	static void
	pubnub_test_rem_socket(struct pubnub *p, void *ctx_data, int fd)
	{
		remSock = fd;
	}

	static void
	pubnub_test_timeout(struct pubnub *p, void *ctx_data, const struct timespec *ts,
			void (*cb)(struct pubnub *p, void *cb_data), void *cb_data)
	{
	}

	static void
	pubnub_test_wait(struct pubnub *p, void *ctx_data)
	{
	}

	static void 
	pubnub_test_stop_wait(struct pubnub *p, void *ctx_data)
	{
	}

	static bool cbCalled;
	static pubnub_res cbResult;
	static char **cbChannels;

	static void subCb(struct pubnub *p, enum pubnub_res result, char **channels, struct json_object *response, void *ctx_data, void *call_data)
	{
		cbCalled = true;
		cbResult = result;
		cbChannels = channels;
	}

	void GetErr() {
        fflush(stderr);
        std::string buf;
        const int bufSize = 1024;
        buf.resize(bufSize);
        int bytesRead = 0;
        if (!eof(_err_pipe[0])) {
            bytesRead = read(_err_pipe[0], &(*buf.begin()), bufSize);
        }
        while(bytesRead == bufSize) {
            _err += buf;
            bytesRead = 0;
            if (!eof(_err_pipe[0])) {
                bytesRead = read(_err_pipe[0], &(*buf.begin()), bufSize);
            }
        }
        if (bytesRead > 0) {
            buf.resize(bytesRead);
            _err += buf;
        }
	}

	virtual void SetUp() {
		memset(&cb, 0, sizeof(cb));
		cb.add_socket = pubnub_test_add_socket;
		cb.rem_socket = pubnub_test_rem_socket;
		cb.timeout = pubnub_test_timeout;
		cb.wait = pubnub_test_wait;
		cb.stop_wait = pubnub_test_stop_wait;

		curlInit = false;

        _err_pipe[0] = 0;
        _err_pipe[1] = 0;
		_old_err = 0;

		_err.clear();
        if (_pipe(_err_pipe, 65536, O_BINARY) != -1) {
			_old_err = dup(fileno(stderr));
			fflush(stderr);
			dup2(_err_pipe[1], fileno(stderr));
		}

		p = pubnub_init("demo", "demo", &cb, NULL);

		addSock = remSock = 0;
		addSockMode = 0;

		cbCalled = false;
	}

	virtual void TearDown() {
        if (_old_err > 0) {
			dup2(_old_err, fileno(stderr));
            close(_old_err);
		}
        if (_err_pipe[0] > 0) {
            close(_err_pipe[0]);
		}
        if (_err_pipe[1] > 0) {
            close(_err_pipe[1]);
		}
		pubnub_done(p);
	}
};

int PubnubTest::addSock, PubnubTest::addSockMode, PubnubTest::remSock;
bool PubnubTest::cbCalled;
pubnub_res PubnubTest::cbResult;
char **PubnubTest::cbChannels;

TEST_F(PubnubTest, Publish) {
	ASSERT_TRUE(curlInit);
	json_object *msg;
	msg = json_object_new_object();
	json_object_object_add(msg, "str", json_object_new_string("test"));
	pubnub_publish(p, "channel", msg, -1, NULL, NULL);
	json_object_put(msg);
	curl_easy_getinfo(p->curl, CURLINFO_EFFECTIVE_URL, &url);
	EXPECT_STREQ("http://pubsub.pubnub.com/publish/demo/demo/0/channel/0/%7B%20%22str%22%3A%20%22test%22%20%7D", url);
}

TEST_F(PubnubTest, Subscribe) {
	ASSERT_TRUE(curlInit);
	pubnub_subscribe(p, "channel", -1, NULL, NULL);
	curl_easy_getinfo(p->curl, CURLINFO_EFFECTIVE_URL, &url);
	char *t = strchr(url, '=');
	ASSERT_TRUE(t != NULL);
	char *s = strdup(url);
	s[t - url] = 0;
	EXPECT_STREQ("http://pubsub.pubnub.com/subscribe/demo/channel/0/0?uuid", s);
	free(s);
	pubnub_connection_cancel(p);
}

TEST_F(PubnubTest, Unsubscribe) {
	ASSERT_TRUE(curlInit);
	const char *channel = "channel";
	pubnub_subscribe(p, channel, -1, NULL, NULL);
	pubnub_unsubscribe(p, &channel, 1, -1, NULL, NULL);
	EXPECT_EQ(1, p->channelset.n);
	pubnub_connection_cancel(p);
	pubnub_subscribe(p, channel, -1, NULL, NULL);
	pubnub_unsubscribe(p, &channel, 1, -1, NULL, NULL);
	EXPECT_EQ(0, p->channelset.n);
}


TEST_F(PubnubTest, SubscribeMulti) {
	ASSERT_TRUE(curlInit);
	const char *channels[] = {"channel1", "channel2"};
	pubnub_subscribe_multi(p, channels, 2, -1, NULL, NULL);
	curl_easy_getinfo(p->curl, CURLINFO_EFFECTIVE_URL, &url);
	char *t = strchr(url,'=');
	ASSERT_TRUE(t != NULL);
	char *s = strdup(url);
	s[t - url] = 0;
	EXPECT_STREQ("http://pubsub.pubnub.com/subscribe/demo/channel1%2Cchannel2/0/0?uuid", s);
	free(s);
	pubnub_connection_cancel(p);
}

TEST_F(PubnubTest, History) {
	ASSERT_TRUE(curlInit);
	pubnub_history(p, "channel", 10, -1, NULL, NULL);
	curl_easy_getinfo(p->curl, CURLINFO_EFFECTIVE_URL, &url);
	EXPECT_STREQ("http://pubsub.pubnub.com/history/demo/channel/0/10", url);
}

TEST_F(PubnubTest, HereNow) {
	ASSERT_TRUE(curlInit);
	pubnub_here_now(p, "channel", -1, NULL, NULL);
	curl_easy_getinfo(p->curl, CURLINFO_EFFECTIVE_URL, &url);
	EXPECT_STREQ("http://pubsub.pubnub.com/v2/presence/sub-key/demo/channel/channel", url);
}

TEST_F(PubnubTest, Time) {
	ASSERT_TRUE(curlInit);
	pubnub_time(p, -1, NULL, NULL);
	curl_easy_getinfo(p->curl, CURLINFO_EFFECTIVE_URL, &url);
	EXPECT_STREQ("http://pubsub.pubnub.com/time/0", url);
}

TEST(ChannelSetTest, AddRemove) {
	struct channelset cs = {NULL, 0};
	EXPECT_EQ(0, channelset_add(&cs, &cs));
	const char *ch1[] = {"abc", "cde"};
	struct channelset cs1 = {ch1, 2};
	EXPECT_EQ(2, channelset_add(&cs, &cs1));
	EXPECT_STREQ(ch1[1], cs.set[1]);
	channelset_done(&cs);
	EXPECT_EQ(0, channelset_add(&cs1, &cs1));
	const char *ch2[] = {"abc", "fgh", "cde"};
	struct channelset cs2 = {ch2, 3};
	struct channelset cs3 = {NULL, 0};
	EXPECT_EQ(2, channelset_add(&cs3, &cs1));
	EXPECT_EQ(1, channelset_add(&cs3, &cs2));
	EXPECT_EQ(2, channelset_rm(&cs3, &cs1));
	EXPECT_EQ(0, channelset_rm(&cs3, &cs1));
	EXPECT_STREQ(ch2[1], cs3.set[0]);
	EXPECT_EQ(1, channelset_rm(&cs3, &cs2));
	channelset_done(&cs3);
}

TEST_F(PubnubTest, SubscribeHttpCbWithError) {
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_TIMEOUT, NULL, NULL, http_data);
	EXPECT_TRUE(cbCalled && cbResult == PNR_TIMEOUT);
	GetErr();
	EXPECT_TRUE(_err.empty());
}

TEST_F(PubnubTest, SubscribeHttpCbWithObject) {
	json_object *response = json_object_new_object();
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_FALSE(cbCalled);
	GetErr();
	EXPECT_TRUE(strstr(_err.c_str(), "Unexpected"));
	EXPECT_TRUE(strstr(_err.c_str(), "reissuing"));
	json_object_put(response);
}

TEST_F(PubnubTest, SubscribeHttpCbWithObjectNoRetry) {
	json_object *response = json_object_new_object();
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	p->error_retry_mask = 0;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_TRUE(cbCalled && cbResult == PNR_FORMAT_ERROR);
	GetErr();
	EXPECT_TRUE(strstr(_err.c_str(), "Unexpected"));
	EXPECT_FALSE(strstr(_err.c_str(), "reissuing"));
	json_object_put(response);
}

TEST_F(PubnubTest, SubscribeHttpCbWithArray) {
	json_object *response = json_object_new_array();
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_FALSE(cbCalled);
	GetErr();
	EXPECT_TRUE(strstr(_err.c_str(), "Unexpected"));
	EXPECT_TRUE(strstr(_err.c_str(), "reissuing"));
	json_object_put(response);
}

TEST_F(PubnubTest, SubscribeHttpCbWithArrayArray) {
	json_object *response = json_object_new_array();
	json_object_array_add(response, json_object_new_array());
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_FALSE(cbCalled);
	GetErr();
	EXPECT_TRUE(strstr(_err.c_str(), "Unexpected"));
	EXPECT_TRUE(strstr(_err.c_str(), "reissuing"));
	json_object_put(response);
}

TEST_F(PubnubTest, SubscribeHttpCbWithArrayArrayTimeToken) {
	json_object *response = json_object_new_array();
	json_object_array_add(response, json_object_new_array());
	char *time_token = "time_token";
	json_object_array_add(response, json_object_new_string(time_token));
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_STREQ(time_token, p->time_token);
	EXPECT_TRUE(cbCalled && cbResult == PNR_OK);
	json_object_put(response);
}

TEST_F(PubnubTest, SubscribeHttpCbWithEncryptedArray) {
	json_object *response = json_object_new_array();
	json_object *msg = json_object_new_array();
	char *msg_str = "42";
	pubnub_set_cipher_key(p, "cipher key");
	json_object_array_add(msg, pubnub_encrypt(p->cipher_key, msg_str));
	json_object_array_add(response, msg);
	json_object_array_add(response, json_object_new_string(""));
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_TRUE(cbCalled && cbResult == PNR_OK);
	json_object *o = json_object_array_get_idx(response, 0);
	const char *s = json_object_get_string(json_object_array_get_idx(o, 0));
	EXPECT_STREQ(msg_str, s);
	json_object_put(response);
}

TEST_F(PubnubTest, SubscribeHttpCbWithChannelsetObject) {
	json_object *response = json_object_new_array();
	json_object_array_add(response, json_object_new_array());
	json_object_array_add(response, json_object_new_string(""));
	json_object_array_add(response, json_object_new_object());
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_FALSE(cbCalled);
	GetErr();
	EXPECT_TRUE(strstr(_err.c_str(), "Unexpected"));
	EXPECT_TRUE(strstr(_err.c_str(), "reissuing"));
	json_object_put(response);
}

TEST_F(PubnubTest, SubscribeHttpCbWithChannelset) {
	json_object *response = json_object_new_array();
	json_object *msg = json_object_new_array();
	json_object_array_add(msg, json_object_new_string(""));
	json_object_array_add(msg, json_object_new_string(""));
	json_object_array_add(msg, json_object_new_string(""));
	json_object_array_add(response, msg);
	json_object_array_add(response, json_object_new_string(""));
	json_object_array_add(response, json_object_new_string("abc,def"));
	struct pubnub_subscribe_cb_http_data *http_data = (struct pubnub_subscribe_cb_http_data *)calloc(1, sizeof(*http_data));
	http_data->cb = subCb;
	pubnub_subscribe_http_cb(p, PNR_OK, response, NULL, http_data);
	EXPECT_TRUE(cbCalled && cbResult == PNR_OK);
	ASSERT_TRUE(cbChannels != NULL);
	EXPECT_STREQ("abc", cbChannels[0]);
	EXPECT_STREQ("def", cbChannels[1]);
	EXPECT_STREQ("", cbChannels[2]);
	EXPECT_TRUE(cbChannels[3] == NULL);
	for(char **p = cbChannels; *p; p++) {
		free(*p);
	}
	free(cbChannels);
	json_object_put(response);
}

}
