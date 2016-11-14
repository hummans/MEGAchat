#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#include "karereCommon.h"
#include "sdkApi.h"
#include "contactList.h"
#include <memory>
#include <map>
#include <type_traits>
#include <retryHandler.h>
#include <serverListProviderForwards.h>
#include "userAttrCache.h"
#include "chatd.h"
#include "IGui.h"
#include <base/trackDelete.h>

namespace strophe { class Connection; }

namespace mega { class MegaTextChat; class MegaTextChatList; }

namespace strongvelope { class ProtocolHandler; }

struct sqlite3;
class Buffer;

namespace karere
{
namespace rh { class IRetryController; }

/** @brief
 * Utulity function to create an application directory, suitable for desktop systems.
 * It reads the env variable KRDIR for a path to the app dir, and if not present,
 * defaults to '~/.karere'.
 */
KARERE_IMPEXP const std::string& createAppDir(const char* dirname=".karere", const char* envVarName="KRDIR");

class TextModule;
class ChatRoom;
class GroupChatRoom;
class Contact;
class ContactList;

typedef std::map<Id, chatd::Priv> UserPrivMap;
class ChatRoomList;

/** @brief An abstract class representing a chatd chatroom. It has two
 * descendants - \c PeerChatRoom, representing a 1on1 chatroom,
 * and \c GroupChatRoom, representing a group chat room. This class also
 * serves as a chat event handler for the chatroom, until the application creates
 * one via \c IApp::createChatHandler()
 */
class ChatRoom: public chatd::Listener, public TrackDelete
{
    //@cond PRIVATE
public:
    ChatRoomList& parent;
protected:
    IApp::IChatHandler* mAppChatHandler = nullptr;
    uint64_t mChatid;
    std::string mUrl;
    unsigned char mShardNo;
    bool mIsGroup;
    chatd::Priv mOwnPriv;
    chatd::Chat* mChat = nullptr;
    bool mIsInitializing = true;
    std::string mTitleString;
    void notifyTitleChanged();
    void synchronousNotifyTitleChanged();
    bool syncRoomPropertiesWithApi(const ::mega::MegaTextChat& chat);
    void switchListenerToApp();
    void createChatdChat(const karere::SetOfIds& initialUsers); //We can't do the join in the ctor, as chatd may fire callbcks synchronously from join(), and the derived class will not be constructed at that point.
public:
    virtual bool syncWithApi(const mega::MegaTextChat& chat) = 0;
    virtual IApp::IChatListItem* roomGui() = 0;
    /** @endcond PRIVATE */

    /** @brief The text that will be displayed on the chat list for that chat */
    virtual const std::string& titleString() const = 0;

    /**
     * @brief The current presence status of the chat. If this is a 1on1 chat, this is
     * the same as the presence of the peer. If it is a groupchat, it is
     * derived from the chatd chatroom status
     */
    virtual Presence presence() const = 0;

    /** @brief Connects to the chatd chatroom */
    virtual void connect() = 0;

    ChatRoom(ChatRoomList& parent, const uint64_t& chatid, bool isGroup, const std::string& url,
             unsigned char shard, chatd::Priv ownPriv, const std::string& aTitle=std::string());

    virtual ~ChatRoom(){}

    /** @brief returns the chatd::Chat chat object associated with the room */
    chatd::Chat& chat() { return *mChat; }

    /** @brief returns the chatd::Chat chat object associated with the room */
    const chatd::Chat& chat() const { return *mChat; }

    /** @brief The chatid of the chatroom */
    const uint64_t& chatid() const { return mChatid; }

    /** @brief Whether this chatroom is a groupchat or 1on1 chat */
    bool isGroup() const { return mIsGroup; }

    /** @brief The websocket url that is used to connect to chatd for that chatroom. Contains an authentication token */
    const std::string& url() const { return mUrl; }

    /** @brief The chatd shart number for that chatroom */
    unsigned char shardNo() const { return mShardNo; }

    /** @brief Our own privilege within this chat */
    chatd::Priv ownPriv() const { return mOwnPriv; }

    /** @brief Whether we are currently member of the chatroom (for group
      * chats), or we are contacts with the peer (for 1on1 chats)
      */
    bool isActive() const { return mOwnPriv != chatd::PRIV_NOTPRESENT; }

    /** @brief The online state reported by chatd for that chatroom */
    chatd::ChatState chatdOnlineState() const { return mChat->onlineState(); }

    /** @brief send a notification to the chatroom that the user is typing. */
    virtual void sendTypingNotification() {}

    /** @brief The application-side event handler that receives events from
     * the chatd chatroom and events about title, online status and unread
     * message count change.
     */
    IApp::IChatHandler* appChatHandler() { return mAppChatHandler; }
    /** @brief Attaches an app-provided event handler to the chatroom
     * The handler must forward some events to the chatroom in order to
     * have the chat list item still receive events. The events that need
     * to be forwarded are:
     * \c onUserJoin, \c onUserLeave, \c onUnreadChanged,
     * \c onOnlineStateChange, \c onRecvNewMessage, \c onRecvHistoryMessage.
     * @param handler The application-provided chat event handler.
     * The chatroom object does not take owhership of the handler,
     * so, on removal, the app should take care to free it if needed.
     */
    void setAppChatHandler(IApp::IChatHandler* handler);
    /** @brief Removes the application-supplied chat event handler from the
     * room. It is up to the aplication to destroy it if needed.
     */
    void removeAppChatHandler();

    /** @brief Whether the chatroom object is currently being
     * constructed.
     */
    bool isInitializing() const { return mIsInitializing; }

    /** @brief Initiates a webrtc call in the chatroom
     *  @param av Whether to initially send video and/or audio
     */
    virtual promise::Promise<void> mediaCall(AvFlags av) = 0;

    /**
     * @brief Updates the chatd url of the chatroom, by asking the API
     */
    promise::Promise<void> updateUrl();

    //chatd::Listener implementation
    virtual void init(chatd::Chat& messages, chatd::DbInterface *&dbIntf);
    virtual void onRecvNewMessage(chatd::Idx, chatd::Message&, chatd::Message::Status);
    virtual void onMessageStatusChange(chatd::Idx idx, chatd::Message::Status newStatus, const chatd::Message &msg);
    virtual void onExcludedFromRoom() {}
//  virtual void onHistoryTruncated();
    virtual void onMsgOrderVerificationFail(const chatd::Message& msg, chatd::Idx idx, const std::string& errmsg)
    {
        KR_LOG_ERROR("msgOrderFail[chatid: %s, msgid %s, userid %s]: %s",
            karere::Id(mChatid).toString().c_str(),
            msg.id().toString().c_str(), msg.userid.toString().c_str(),
            errmsg.c_str());
    }

};
/** @brief Represents a 1on1 chatd chatroom */
class PeerChatRoom: public ChatRoom
{
//  @cond PRIVATE
protected:
    uint64_t mPeer;
    chatd::Priv mPeerPriv;
    Contact& mContact;
    // mRoomGui must be the last member, since when we initialize it,
    // we call into the app and pass our this pointer, so all other members
    // must be initialized
    IApp::IPeerChatListItem* mRoomGui;
    friend class ContactList;
    IApp::IPeerChatListItem* addAppItem();
    virtual bool syncWithApi(const mega::MegaTextChat& chat);
    bool syncOwnPriv(chatd::Priv priv);
    bool syncPeerPriv(chatd::Priv priv);
    static uint64_t getSdkRoomPeer(const ::mega::MegaTextChat& chat);
    void updatePresence();
    void initWithChatd();
    virtual void connect();
    inline Presence calculatePresence(Presence pres) const;
    void updateTitle(const std::string& title);
    friend class Contact;
    friend class ChatRoomList;
    PeerChatRoom(ChatRoomList& parent, const uint64_t& chatid, const std::string& url,
            unsigned char shard, chatd::Priv ownPriv, const uint64_t& peer, chatd::Priv peerPriv);
    PeerChatRoom(ChatRoomList& parent, const mega::MegaTextChat& room);
    ~PeerChatRoom();
    //@endcond
public:
    virtual IApp::IChatListItem* roomGui() { return mRoomGui; }
    /** @brief The userid of the other person in the 1on1 chat */
    const uint64_t peer() const { return mPeer; }
    chatd::Priv peerPrivilege() const { return mPeerPriv; }
    /** @brief The contact object representing the peer of the 1on1 chat */
    Contact& contact() const { return mContact; }

    /** @brief The screen name of the peer */
    virtual const std::string& titleString() const;

    /** @brief The presence status of the chatroom. Derived from the presence
     * of the peer and the status of the chatroom reported by chatd. For example,
     * the peer may be online, but the chatd connection may be offline or there may
     * be a problem joining the chatroom on chatd. In such a case, the presence
     * will be `offline`
     */
    virtual Presence presence() const;
    promise::Promise<void> mediaCall(AvFlags av);
/** @cond PRIVATE */
    //chatd::Listener interface
    virtual void onUserJoin(Id userid, chatd::Priv priv);
    virtual void onUserLeave(Id userid);
    virtual void onOnlineStateChange(chatd::ChatState state);
    virtual void onUnreadChanged();
/** @endcond */
};

/** @brief Represents a chatd chatroom that is a groupchat */
class GroupChatRoom: public ChatRoom
{
public:
    /** @brief Represents a single chatroom member */
    class Member
    {
    protected:
        GroupChatRoom& mRoom;
        uint64_t mHandle;
        chatd::Priv mPriv;
        uint64_t mNameAttrCbHandle;
        std::string mName;
        void subscribeForNameChanges();
    public:
        Member(GroupChatRoom& aRoom, const uint64_t& user, chatd::Priv aPriv);
        ~Member();

        /** @brief The current display name of the member */
        const std::string& name() const { return mName; }

        /** @brief The current provilege of the member within the groupchat */
        chatd::Priv priv() const { return mPriv; }
        friend class GroupChatRoom;
    };
    /**
     * @brief A map that holds all the members of a group chat room, keyed by the userid */
    typedef std::map<uint64_t, Member*> MemberMap;

    /** @cond PRIVATE */
    protected:
    MemberMap mPeers;
    bool mHasTitle;
    std::string mEncryptedTitle; //holds the encrypted title until we create the strongvelope module
    IApp::IGroupChatListItem* mRoomGui;
    void syncRoomPropertiesWithApi(const mega::MegaTextChat &chat);
    bool syncMembers(const UserPrivMap& users);
    static UserPrivMap& apiMembersToMap(const mega::MegaTextChat& chat, UserPrivMap& membs);
    void loadTitleFromDb();
    promise::Promise<void> decryptTitle();
    void clearTitle();
    void updateAllOnlineDisplays(Presence pres);
    void addMember(uint64_t userid, chatd::Priv priv, bool saveToDb);
    bool removeMember(uint64_t userid);
    virtual bool syncWithApi(const mega::MegaTextChat &chat);
    IApp::IGroupChatListItem* addAppItem();
    virtual IApp::IChatListItem* roomGui() { return mRoomGui; }
    void deleteSelf(); //<Deletes the room from db and then immediately destroys itself (i.e. delete this)
    void makeTitleFromMemberNames();
    void initWithChatd();
    void setRemoved();
    void notifyRemoved();
    void notifyRejoined();
    virtual void connect();

    friend class ChatRoomList;
    friend class Member;
    GroupChatRoom(ChatRoomList& parent, const mega::MegaTextChat& chat);
    GroupChatRoom(ChatRoomList& parent, const uint64_t& chatid, const std::string& aUrl,
                  unsigned char aShard, chatd::Priv aOwnPriv, const std::string& title);
    ~GroupChatRoom();
public:
    promise::Promise<void> setPrivilege(karere::Id userid, chatd::Priv priv);
    promise::Promise<void> setTitle(const std::string& title);
    promise::Promise<void> leave();
    promise::Promise<void> invite(uint64_t userid, chatd::Priv priv);
    virtual promise::Promise<void> mediaCall(AvFlags av);
//chatd::Listener
    void onUserJoin(Id userid, chatd::Priv priv);
    void onUserLeave(Id userid);
    void onOnlineStateChange(chatd::ChatState);
    /** @endcond PRIVATE */

    /** @brief Returns the map of the users in the chatroom, except our own user */
    const MemberMap& peers() const { return mPeers; }

    /** @brief Returns whether the group chatroom has a title set. If not, then
      * its title string will be composed from the first names of the room members
      */
    bool hasTitle() const { return mHasTitle; }

    /** @brief The title of the chatroom */
    virtual const std::string& titleString() const { return mTitleString; }

    /** @brief The 'presence' of the chatroom - it's actually the online state,
     * and can be only online or offline, depending on whether we are connected
     * to the chatd chatroom
     */
    virtual Presence presence() const
    {
        return (mChat->onlineState() == chatd::kChatStateOnline)
                ? Presence::kOnline
                : Presence::kOffline;
    }

    /** @brief Removes the specifid user from the chatroom. You must have
     * operator privileges to do that.
     * @param user The handle of the user to remove from the chatroom.
     * @returns A promise with the MegaRequest result, returned by the mega SDK.
     */
    promise::Promise<void> excludeMember(uint64_t user);
};

/** @brief Represents all chatd chatrooms that we are members of at the moment,
 * keyed by the chatid of the chatroom. This object can be obtained
 * via \c Client::chats
 */
class ChatRoomList: public std::map<uint64_t, ChatRoom*> //don't use shared_ptr here as we want to be able to immediately delete a chatroom once the API tells us it's deleted
{
/** @cond PRIVATE */
public:
    Client& client;
    void addMissingRoomsFromApi(const mega::MegaTextChatList& rooms, karere::SetOfIds& chatids);
    ChatRoom& addRoom(const mega::MegaTextChat &room);
    void removeRoom(GroupChatRoom& room);
    ChatRoomList(Client& aClient);
    ~ChatRoomList();
    void loadFromDb();
    void onChatsUpdate(const std::shared_ptr<mega::MegaTextChatList>& chats);
/** @endcond PRIVATE */
};

/** @brief Represents a karere contact. Also handles presence change events. */
class Contact: public IPresenceListener, public karere::TrackDelete
{
/** @cond PRIVATE */
protected:
    ContactList& mClist;
    uint64_t mUserid;
    PeerChatRoom* mChatRoom;
    uint64_t mUsernameAttrCbId;
    std::string mEmail;
    int64_t mSince;
    std::string mTitleString;
    int mVisibility;
    IApp::IContactListHandler* mAppClist; //cached, because we often need to check if it's null
    IApp::IContactListItem* mDisplay; //must be after mTitleString because it will read it
    std::shared_ptr<XmppContact> mXmppContact; //after constructor returns, we are guaranteed to have this set to a vaild instance
    bool mIsInitializing = true;
    void updateTitle(const std::string& str);
    void notifyTitleChanged();
    void setChatRoom(PeerChatRoom& room);
    void attachChatRoom(PeerChatRoom& room);
    friend class PeerChatRoom;
public:
    Contact(ContactList& clist, const uint64_t& userid, const std::string& email,
            int visibility, int64_t since, PeerChatRoom* room = nullptr);
    ~Contact();
/** @endcond PRIVATE */

    /** @brief The contactlist object which this contact is member of */
    ContactList& contactList() const { return mClist; }

    /** @brief The XMPP contact associated with this Mega contact. It provides
     * the presence notifications
     */
    XmppContact& xmppContact() { return *mXmppContact; }

    /** @brief Returns the 1on1 chatroom with this contact, if one exists.
     * Otherwise returns NULL
     */
    PeerChatRoom* chatRoom() { return mChatRoom; }
    /** @brief The \c IApp::IContactListItem that is associated with this
     * contact. Can be NULL if there is no IContactListHandler or it returned
     * NULL from \c addContactListItem()
     */
    IApp::IContactListItem* appItem() const { return mDisplay; }

    /** @brief Creates a 1on1 chatroom with this contact, if one does not exist,
     * otherwise returns the existing one.
     * @returns a promise containing the 1on1 chatroom object
     */
    promise::Promise<ChatRoom *> createChatRoom();

    /** @brief Returns the current screen name of this contact */
    const std::string& titleString() const { return mTitleString; }

    /** @brief Returns the userid of this contact */
    uint64_t userId() const { return mUserid; }

    /** @brief Returns the email of this contact */
    const std::string& email() const { return mEmail; }

    /** @brief Retutns the bare JID, representing this contact in XMPP */
    const std::string& jid() const { return mXmppContact->bareJid(); }

    /** @brief Returns the time since this contact was added */
    int64_t since() const { return mSince; }

    /** @brief The visibility of this contact, as returned by
     * mega::MegaUser::getVisibility(). If it is \c MegaUser::VISIBILITY_HIDDEN,
     * then this contact is not a contact anymore, but kept in the contactlist
     * to be able to access archived chat history etc.
     */
    int visibility() const { return mVisibility; }

    /** @brief The presence of the contact */
    Presence presence() const { return mXmppContact->presence(); }

    /** @cond PRIVATE */
    virtual void onPresence(Presence pres)
    {
        if (mChatRoom && (mChatRoom->chatdOnlineState() != chatd::kChatStateOnline))
            pres = Presence::kOffline;
        updateAllOnlineDisplays(pres);
    }
    void onVisibilityChanged(int newVisibility)
    {
        mVisibility = newVisibility;
        if (mDisplay)
            mDisplay->onVisibilityChanged(newVisibility);
    }
    void updateAllOnlineDisplays(Presence pres)
    {
        if (mDisplay)
            mDisplay->onPresenceChanged(pres);
        if (mChatRoom)
            mChatRoom->updatePresence();
    }
    friend class ContactList;
};

/** @brief This is the karere contactlist class. It maps user ids
 * to Contact objects
 */
class ContactList: public std::map<uint64_t, Contact*>
{
protected:
    void removeUser(iterator it);
    void removeUser(uint64_t userid);
public:
    /** @brief The Client object that this contactlist belongs to */
    Client& client;

    /** @brief Returns the contact object from the specified XMPP jid if one exists,
     * otherwise returns NULL
     */
    Contact* contactFromJid(const std::string& jid) const;
    Contact& contactFromUserId(uint64_t userid) const;

    /** @cond PRIVATE */
    ContactList(Client& aClient);
    ~ContactList();
    void loadFromDb();
    bool addUserFromApi(mega::MegaUser& user);
    void onUserAddRemove(mega::MegaUser& user); //called for actionpackets
    promise::Promise<void> removeContactFromServer(uint64_t userid);
    void syncWithApi(mega::MegaUserList& users);
    IApp::IContactListItem& attachRoomToContact(const uint64_t& userid, PeerChatRoom &room);
    void onContactOnlineState(const std::string& jid);
    const std::string* getUserEmail(uint64_t userid) const;
    /** @endcond */
};
/** @brief The karere Client object. Create an instance to use Karere.
 *
 *  A sequence of how the client has to be initialized:
 *  1. create a new MegaApi instance of the Mega SDK
 *  2. create a new karere::Client instance and pass it the Mega SDK instance
 *  3. Call MegaApi::login() and wait for completion
 *  4. Call MegaApi::fetchnodes() and wait for completion
 *     [at this stage, cloud storage apps show the main GUI, but apps with
 *      with chat enabled are not ready to be shown yet]
 *  5. Call karere::Client::init() to initialize the chat engine.
 *     [at this stage, a chat-enabled app can load chatrooms and history
 *      from the local karere cache db, and can operate in offline mode]
 *  6. Call karere::Client::connect() and wait for completion
 *  7. The app is ready to operate
 */
class Client: public rtcModule::IGlobalEventHandler, mega::MegaGlobalListener
{
/** @cond PRIVATE */
protected:
    std::string mAppDir;
//these must be before the db member, because they are initialized during the init of db member
    Id mMyHandle = mega::UNDEF;
    std::string mSid;
    std::unique_ptr<UserAttrCache> mUserAttrCache;
    std::string mMyEmail;
    bool mConnected = false;
public:
    sqlite3* db = nullptr;
    std::shared_ptr<strophe::Connection> conn;
    std::unique_ptr<chatd::Client> chatd;
    MyMegaApi api;
    rtcModule::IRtcModule* rtc = nullptr;
    bool isTerminating = false;
    unsigned mReconnectConnStateHandler = 0;
    IApp& app;
    char mMyPrivCu25519[32] = {0};
    char mMyPrivEd25519[32] = {0};
    char mMyPrivRsa[1024] = {0};
    unsigned short mMyPrivRsaLen = 0;
    char mMyPubRsa[512] = {0};
    unsigned short mMyPubRsaLen = 0;
    std::unique_ptr<IApp::ILoginDialog> mLoginDlg;
    bool skipInactiveChatrooms = false;
    UserAttrCache& userAttrCache() const { return *mUserAttrCache; }
    bool contactsLoaded() const { return mContactsLoaded; }
    bool connected() const { return mConnected; }
    std::vector<std::shared_ptr<::mega::MegaTextChatList>> mInitialChats;
    /** @endcond PRIVATE */

    /** @brief The contact list of the client */
    std::unique_ptr<ContactList> contactList;

    /** @brief The list of chats that we are member of */
    std::unique_ptr<ChatRoomList> chats;

    const Id myHandle() const { return mMyHandle; }

    /** @brief Our own display name */
    const std::string& myName() const { return mMyName; }

    /** @brief Utulity function to convert a jid to a user handle */
    static uint64_t useridFromJid(const std::string& jid);

    /** @brief The XMPP resource of our XMPP connection */
    std::string getResource() const
    {
        return strophe::getResourceFromJid(conn->fullJid());
    }

    /**
     * @brief Creates a karere Client.
     *
     * @param sdk The Mega SDK instance that this client will use.
     * @param app The IApp interface that the client will use to access
     * services/objects implemented by the application.
     * @param pres The initial presence that will be set when we log in.
     * @param existingCache Whether the karere cache db exists and karere should
     * try to use it, or not. If \c true is specified but the db is unreadable or
     * inconsistent, karere will behave as if \c false was specified - will
     * delete the karere.db file and re-create it from scratch.
     */
    Client(::mega::MegaApi& sdk, IApp& app, const std::string& appDir,
           Presence pres);

    virtual ~Client();

    /**
     * @brief Performs karere-only login, assuming the Mega SDK is already logged in
     * with an existing session.
     */
    promise::Promise<void> initWithExistingSession();

    /**
     * @brief Performs karere-only login, assuming the Mega SDK is already logged
     * in with a new session
     */
    promise::Promise<void> initWithNewSession();

    /**
     * @brief Initializes karere, opening or creating the local db cache
     * @param useCache - \c true if the mega SDK was logged in with an existing
     * session (the karere cache MUST exist in that case, otherwise an
     * error is returned), or \false if the SDK just created a new session, in which
     * case the cache db is created and initialized from scratch.
     */
    promise::Promise<void> init(bool useCache);

    /** @brief Does the actual connection to chatd, xmpp and gelb. Assumes the
     * Mega SDK is already logged in. This must be called after
     * \c initNewSession() or \c initExistingSession() completes */
    promise::Promise<void> connect();

    /**
     * @brief A convenience method that logs in the Mega SDK and then inits
     * karere. This can be used when building a standalone chat app where there
     * is no app code that logs in the Mega SDK.
     * @param sid - The mega session id with which to log in the SDK and init
     * karere. If it is NULL, then a new SDK session is created.
     */
    promise::Promise<void> loginSdkAndInit(const char* sid);

    /** @brief Notifies the client that network connection is down */
    void notifyNetworkOffline();

    /** @brief Notifies the client that internet connection is again available */
    void notifyNetworkOnline();

    void startKeepalivePings();

    /** Terminates the karere client, logging it out, hanging up calls,
     * and cleaning up state
     * @param deleteDb - if set to \c true, deletes the local cache db.
     */
    promise::Promise<void> terminate(bool deleteDb=false);

    promise::Promise<std::string> getUserFullName(Id userid);
    /**
     * @brief Ping a target peer to check whether he/she is alive
     *
     * @param [peerJid] {const char*} peer's Jid. If NULL, then no 'to'
     * attribute will be included in the stanza, effectively sending the ping to the server
     * @param [intervalSec] {int} optional with default value as 100, interval in seconds to do ping.
     *
     * This performs a xmpp ping to the target jid to check if the user is alive or not.
     */
    strophe::StanzaPromise pingPeer(const char* peerJid);

    /**
    * @brief set user's chat presence.
    * Set user's presence state
    *
    * @param force Forces re-setting the presence, even if the current presence
    * is the same. Normally is \c false
    */
    promise::Promise<void> setPresence(const Presence pres, bool force = false);

    /** Returns the XMPP contactlist, as obtained from the XMPP server */
    XmppContactList& xmppContactList()
    {
        return mXmppContactList;
    }

    /** @brief Creates a group chatroom with the specified peers, privileges
     * and title.
     * @param peers A vector of userhandle and privilege pairs for each of
     * the peer users. Our own handle is implicitly added to the groupchat
     * with operator privilega
     * @param title The title of the group chat. If set to an empty string,
     * no title will be set and the room will be shown with the names of
     * the participants.
     */
    promise::Promise<karere::Id>
    createGroupChat(std::vector<std::pair<uint64_t, chatd::Priv>> peers);

/** @cond PRIVATE */
    void dumpChatrooms(::mega::MegaTextChatList& chatRooms);
    void dumpContactList(::mega::MegaUserList& clist);

protected:
    std::string mMyName;
    bool mContactsLoaded = false;
    Presence mOwnPresence;
    /** @brief Our own email address */
    std::string mEmail;
    /** @brief Our password */
    std::string mPassword;
    /** @brief Client's contact list */
    XmppContactList mXmppContactList;
    typedef FallbackServerProvider<HostPortServerInfo> XmppServerProvider;
    std::unique_ptr<XmppServerProvider> mXmppServerProvider;
    std::unique_ptr<rh::IRetryController> mReconnectController;
    xmpp_ts mLastPingTs = 0;
    std::string dbPath() const;
    void openDb();
    void createDb();
    void wipeDb();
    void createDbSchema(sqlite3*& database);
    void connectToChatd();
    karere::Id getMyHandleFromDb();
    karere::Id getMyHandleFromSdk();
    promise::Promise<void> loadOwnKeysFromApi();
    void loadOwnKeysFromDb();
    void loadContactListFromApi();
    strongvelope::ProtocolHandler* newStrongvelope(karere::Id chatid);
    void setupXmppReconnectHandler();
    promise::Promise<void> connectXmpp(const std::shared_ptr<HostPortServerInfo>& server);
    void setupXmppHandlers();
    promise::Promise<int> initializeContactList();
    /** @brief A convenience method to log in the associated Mega SDK instance,
     *  using IApp::ILoginDialog to ask the user/app for credentials. This
     * method is to be used in a standalone chat app where the SDK instance is not
     * logged by other code, like for example the qt test app. THe reason this
     * method does not just accept a user and pass but rather calls back into
     * ILoginDialog is to be able to update the login progress via ILoginDialog,
     * and to save the app the management of the dialog, retries in case of
     * bad credentials etc. This is just a convenience method.
     */
    promise::Promise<void> sdkLoginNewSession();

    /** @brief A convenience method to log the sdk in using an existing session,
     * identified by \c sid. This is to be used in a standalone chat app where
     * there is no existing code that logs in the Mega SDK instance.
     */
    promise::Promise<void> sdkLoginExistingSession(const char* sid);

    /**
     * @brief send response to ping request.
     *
     * This performs an xmpp response to the received xmpp ping request.
     */
    void sendPong(const std::string& peerJid, const std::string& messageId);

    //rtcModule::IGlobalEventHandler interface
    virtual rtcModule::IEventHandler* onIncomingCallRequest(
            const std::shared_ptr<rtcModule::ICallAnswer> &call);
    virtual void discoAddFeature(const char *feature);
    //mega::MegaGlobalListener interface, called by worker thread
    virtual void onChatsUpdate(mega::MegaApi*, mega::MegaTextChatList* rooms);
    virtual void onUsersUpdate(mega::MegaApi*, mega::MegaUserList* users);
    virtual void onContactRequestsUpdate(mega::MegaApi*, mega::MegaContactRequestList* reqs);
    friend class ChatRoom;
/** @endcond PRIVATE */
};

inline Presence PeerChatRoom::calculatePresence(Presence pres) const
{
     if (mChat && mChat->onlineState() != chatd::kChatStateOnline)
         return Presence::kOffline;
     return pres;
}

}
#endif // CHATCLIENT_H
