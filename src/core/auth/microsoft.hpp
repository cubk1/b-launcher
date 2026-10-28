#pragma once

#include <core/auth/account.hpp>
#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace b_launcher_base::auth
{

struct device_code
{
  std::string user_code;
  std::string device_code;
  std::string verification_uri;
  std::string message;
  std::chrono::seconds interval { 5 };
  std::chrono::seconds expires_in { 900 };
};

struct microsoft_token
{
  std::string access_token;
  std::string refresh_token;
  std::int64_t expires_in = 0;
};

struct microsoft_client
{
  std::string client_id;
  std::string scope = "XboxLive.signin offline_access";
  bool live = false;
  std::string tenant = "consumers";
  std::string rps_prefix = "d=";
};

struct pkce_pair
{
  std::string verifier;
  std::string challenge;

  static pkce_pair generate ();
};

class microsoft_authenticator
{
public:
  explicit microsoft_authenticator (std::string client_id,
                                    std::string scope
                                    = "XboxLive.signin offline_access",
                                    std::string tenant = "consumers");
  explicit microsoft_authenticator (microsoft_client client);

  result<device_code> request_device_code (cancellation_token token
                                           = {}) const;
  result<microsoft_token> poll_device_code (const device_code &code,
                                            cancellation_token token
                                            = {}) const;
  std::string authorization_url (std::string_view redirect_uri,
                                 std::string_view state,
                                 const pkce_pair &pkce) const;
  result<microsoft_token> exchange_code (std::string_view code,
                                         std::string_view redirect_uri,
                                         const pkce_pair &pkce,
                                         cancellation_token token = {}) const;
  result<microsoft_token> refresh_token (std::string_view refresh,
                                         cancellation_token token = {}) const;
  result<account> login (const microsoft_token &ms,
                         cancellation_token token = {}) const;
  result<account>
  login_device_flow (const std::function<void (const device_code &)> &show,
                     cancellation_token token = {}) const;
  result<account> refresh (const account &current,
                           cancellation_token token = {}) const;

  static result<account> fetch_profile (account current,
                                        cancellation_token token = {});
  const std::string &client_id () const noexcept;

private:
  std::string token_url () const;

  std::string m_client_id;
  std::string m_scope;
  std::string m_tenant;
  bool m_live = false;
  std::string m_rps_prefix = "d=";
};

}
