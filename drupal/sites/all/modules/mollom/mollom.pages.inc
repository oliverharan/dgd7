<?php
// $Id: mollom.pages.inc,v 1.10 2010/05/06 14:48:04 dries Exp $

/**
 * @file
 * Various non-administration page callbacks for the mollom module.
 */

/**
 * AJAX callback to retrieve a CAPTCHA.
 *
 * @param $type
 *   The new CAPTCHA type to retrieve, e.g. 'image' or 'audio'.
 * @param $form_build_id
 *   The internal form build id of the form to update the CAPTCHA for.
 * @param $mollom_session_id
 *   The last known Mollom session id contained in the form.
 *
 * @return
 *   A JSON array containing:
 *   - content: The HTML markup for the new CAPTCHA.
 *   - session_id: The Mollom session id for the new CAPTCHA.
 *
 * @todo Add error handling.
 */
function mollom_captcha_js($type, $form_build_id, $mollom_session_id) {
  $captcha = mollom_get_captcha($type, array('session_id' => $mollom_session_id));

  // Update cached session id in the cached $form_state.
  // We rely on native form caching of Form API to store our Mollom session
  // data. When retrieving a new CAPTCHA through this JavaScript callback, the
  // cached $form_state is not updated and still contains the Mollom session
  // data that was known when rendering the form. Since above XML-RPC requests
  // may return a new Mollom session id for the new CAPTCHA, the user would not
  // be able to successfully complete the CAPTCHA, because we would try to
  // validate the user's response in combination with the old/previous session
  // id. Therefore, we need to update the session id in the cached $form_state.
  // @todo Replace the entire CAPTCHA switch/refresh with new AJAX framework
  //   functionality.
  if (!empty($captcha['response']['session_id'])) {
    if ($cache = cache_get('form_state_' . $form_build_id, 'cache_form')) {
      $form_state = $cache->data;
      $form_state['mollom']['session_id'] = $captcha['response']['session_id'];
      cache_set('form_state_' . $form_build_id, $form_state, 'cache_form', REQUEST_TIME + 21600);
      // After successfully updating the cache, replace the original session id.
      $mollom_session_id = $captcha['response']['session_id'];
    }
  }

  // Return new content and new session_id via JSON.
  $data = array(
    'content' => $captcha['markup'],
    'session_id' => $mollom_session_id,
  );
  drupal_json_output($data);
  drupal_exit();
}

/**
 * Form builder for report to Mollom form.
 *
 * @param $entity
 *   The entity type of the data to report, e.g. 'node' or 'comment'.
 * @param $id
 *   The entity id the data belongs to. If 'session' is passed as $entity, then
 *   $id is assumed to be a Mollom session_id, as returned by Mollom servers,
 *   which should only be used to report session data that was not stored for an
 *   entity in the database (such as contact form submissions).
 */
function mollom_report_form($form, &$form_state, $entity, $id) {
  $form['entity'] = array(
    '#type' => 'value',
    '#value' => $entity,
  );
  $form['id'] = array(
    '#type' => 'value',
    '#value' => $id,
  );
  $form['feedback'] = array(
    '#type' => 'radios',
    '#title' => t('Optionally report this to Mollom'),
    '#options' => array(
      'none' => t("Don't send feedback to Mollom"),
      'spam' => t('Report as spam or unsolicited advertising'),
      'profanity' => t('Report as obscene, violent or profane content'),
      'low-quality' => t('Report as low-quality content or writing'),
      'unwanted' => t('Report as unwanted, taunting or off-topic content'),
    ),
    '#default_value' => 'none',
    '#description' => t("Mollom is a web service that helps you moderate your site's content: see <a href=\"http://mollom.com\">http://mollom.com</a> for more information. By sending feedback to Mollom, you teach Mollom about the content you like and dislike, allowing Mollom to do a better job helping you moderate your site's content. If you want to report multiple posts at once, you can use Mollom's bulk operations on the content and comment administration pages."),
  );

  return confirm_form($form,
    t('Are you sure you want to delete and report the content as inappropriate?'),
    '<front>',
    t('This action cannot be undone.'),
    t('Delete'), t('Cancel')
  );
}

/**
 * Form submit handler for mollom_report_form().
 */
function mollom_report_form_submit($form, &$form_state) {
  if ($form_state['values']['confirm']) {
    $entity = $form_state['values']['entity'];
    $id = $form_state['values']['id'];

    // Load the Mollom session data.
    if ($entity == 'session') {
      $data = new stdClass;
      $data->session = $id;
    }
    else {
      $data = mollom_data_load($entity, $id);
    }

    // Send feedback to Mollom, if we have session data.
    if (isset($data->session) && isset($form_state['values']['feedback']) && $form_state['values']['feedback'] != 'none') {
      // @todo Check the actual reponse.
      _mollom_send_feedback($data->session, $form_state['values']['feedback']);
      drupal_set_message(t('The content was successfully reported as inappropriate.'));
    }

    // Delete the content. The callback should take care of proper deletion and
    // cache clearing on its own.
    foreach (mollom_form_list() as $form_id => $info) {
      if (!isset($info['entity']) || $info['entity'] != $entity) {
        continue;
      }
      // If there is a 'report delete callback', invoke it.
      if (isset($info['report delete callback']) && function_exists($info['report delete callback'])) {
        $function = $info['report delete callback'];
        $function($entity, $id);
        break;
      }
    }

    $form_state['redirect'] = '<front>';
  }
}