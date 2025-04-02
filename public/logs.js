// Check authentication
const auth = JSON.parse(sessionStorage.getItem('auth'));
if (!auth) {
  window.location.href = '/login.html';
  throw new Error('Not authenticated');
}

const building = new URLSearchParams(window.location.search).get('building') || auth.building;
const dbApp = firebase.initializeApp(firebaseConfig, 'DatabaseApp');
const database = firebase.database(dbApp);

// Load logs
function loadLogs() {
  const logsRef = database.ref(`buildings/${building}/cards`);
  const tableBody = document.getElementById('logsTableBody');
  
  logsRef.on('value', (snapshot) => {
    tableBody.innerHTML = '';
    
    if (!snapshot.exists()) {
      tableBody.innerHTML = '<tr><td colspan="4">No access records found</td></tr>';
      return;
    }

    // Convert to array and sort by time (newest first)
    const logs = [];
    snapshot.forEach(child => logs.push(child.val()));
    logs.sort((a, b) => new Date(b.time) - new Date(a.time));

    logs.forEach(log => {
      const row = document.createElement('tr');
      row.innerHTML = `
        <td>${log.time || 'N/A'}</td>
        <td>${log.uid || 'N/A'}</td>
        <td>${log.user || 'Unknown'}</td>
        <td class="${log.access === 'Granted' ? 'text-success' : 'text-danger'}">
          ${log.access || 'Denied'}
        </td>
      `;
      tableBody.appendChild(row);
    });
  }, (error) => {
    tableBody.innerHTML = `<tr><td colspan="4">Error loading data: ${error.message}</td></tr>`;
  });
}

// Initialize page
document.addEventListener('DOMContentLoaded', () => {
  // Display user info
  document.getElementById('buildingName').textContent = building;
  document.getElementById('userEmail').textContent = auth.email;
  document.getElementById('userRole').textContent = auth.role;

  // Load logs
  loadLogs();

  // Setup admin controls if user is admin
  if (auth.role === 'admin') {
    document.querySelectorAll('.admin-only').forEach(el => el.style.display = 'block');
    setupUserManagement();
  }

  // Logout handler
  document.getElementById('logoutBtn').addEventListener('click', () => {
    sessionStorage.removeItem('auth');
    window.location.href = '/login.html';
  });
});

// Admin user management
function setupUserManagement() {
  const usersRef = database.ref(`buildings/${building}/users`);
  
  // Populate users table
  usersRef.on('value', (snapshot) => {
    const usersTable = document.getElementById('usersTableBody');
    usersTable.innerHTML = '';
    
    snapshot.forEach((userSnapshot) => {
      const user = userSnapshot.val();
      const row = document.createElement('tr');
      
      row.innerHTML = `
        <td>${userSnapshot.key}</td>
        <td>${user.email}</td>
        <td>${user.role}</td>
        <td>
          <button class="btn btn-sm btn-danger delete-user" data-id="${userSnapshot.key}">
            Delete
          </button>
        </td>
      `;
      usersTable.appendChild(row);
    });
    
    // Add delete handlers
    document.querySelectorAll('.delete-user').forEach(btn => {
      btn.addEventListener('click', (e) => {
        if (confirm('Delete this user?')) {
          usersRef.child(e.target.dataset.id).remove()
            .catch(err => alert('Delete failed: ' + err.message));
        }
      });
    });
  });

  // Add new user form
  document.getElementById('addUserForm').addEventListener('submit', (e) => {
    e.preventDefault();
    
    const username = document.getElementById('newUsername').value;
    const email = document.getElementById('newEmail').value;
    const password = document.getElementById('newPassword').value;
    const role = document.getElementById('newRole').value;
    
    usersRef.child(username).set({
      email: email,
      password: password, // In production, hash this first!
      role: role
    })
    .then(() => {
      alert('User added successfully');
      e.target.reset();
    })
    .catch(err => alert('Error adding user: ' + err.message));
  });
}