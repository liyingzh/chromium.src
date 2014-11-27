// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_USER_SELECTION_SCREEN_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_USER_SELECTION_SCREEN_H_

#include <map>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/chromeos/login/ui/login_display.h"
#include "chrome/browser/signin/screenlock_bridge.h"
#include "components/user_manager/user.h"
#include "ui/wm/core/user_activity_observer.h"

namespace chromeos {

class LoginDisplayWebUIHandler;

// This class represents User Selection screen: user pod-based login screen.
class UserSelectionScreen : public wm::UserActivityObserver,
                            public ScreenlockBridge::LockHandler {
 public:
  UserSelectionScreen();
  virtual ~UserSelectionScreen();

  void SetLoginDisplayDelegate(LoginDisplay::Delegate* login_display_delegate);
  void SetHandler(LoginDisplayWebUIHandler* handler);

  static const user_manager::UserList PrepareUserListForSending(
      const user_manager::UserList& users,
      std::string owner,
      bool is_signin_to_add);


  virtual void Init(const user_manager::UserList& users, bool show_guest);
  const user_manager::UserList& GetUsers() const;
  void OnUserImageChanged(const user_manager::User& user);
  void OnBeforeUserRemoved(const std::string& username);
  void OnUserRemoved(const std::string& username);

  void OnPasswordClearTimerExpired();
  virtual void SendUserList();
  void HandleGetUsers();
  // wm::UserActivityDetector implementation:
  virtual void OnUserActivity(const ui::Event* event) override;

  void InitEasyUnlock();

  // ScreenlockBridge::LockHandler implementation:
  virtual void ShowBannerMessage(const base::string16& message) override;
  virtual void ShowUserPodCustomIcon(
      const std::string& user_email,
      const ScreenlockBridge::UserPodCustomIconOptions& icon) override;
  virtual void HideUserPodCustomIcon(const std::string& user_email) override;

  virtual void EnableInput() override;
  virtual void SetAuthType(const std::string& user_email,
                           AuthType auth_type,
                           const base::string16& auth_value) override;
  virtual AuthType GetAuthType(const std::string& user_email) const override;

  virtual void Unlock(const std::string& user_email) override;
  virtual void AttemptEasySignin(const std::string& user_email,
                                 const std::string& secret,
                                 const std::string& key_label) override;

  // Fills |user_dict| with information about |user|.
  static void FillUserDictionary(
      user_manager::User* user,
      bool is_owner,
      bool is_signin_to_add,
      ScreenlockBridge::LockHandler::AuthType auth_type,
      const std::vector<std::string>* public_session_recommended_locales,
      base::DictionaryValue* user_dict);

  // Determines if user auth status requires online sign in.
  static bool ShouldForceOnlineSignIn(const user_manager::User* user);

 protected:
  LoginDisplayWebUIHandler* handler_;
  LoginDisplay::Delegate* login_display_delegate_;

  // Map from public session user IDs to recommended locales set by policy.
  typedef std::map<std::string, std::vector<std::string> >
      PublicSessionRecommendedLocaleMap;
  PublicSessionRecommendedLocaleMap public_session_recommended_locales_;

 private:
  // Whether to show guest login.
  bool show_guest_;

  // Set of Users that are visible.
  user_manager::UserList users_;

  // Map of usernames to their current authentication type. If a user is not
  // contained in the map, it is using the default authentication type.
  std::map<std::string, ScreenlockBridge::LockHandler::AuthType>
      user_auth_type_map_;

  // Timer for measuring idle state duration before password clear.
  base::OneShotTimer<UserSelectionScreen> password_clear_timer_;

  DISALLOW_COPY_AND_ASSIGN(UserSelectionScreen);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_USER_SELECTION_SCREEN_H_
